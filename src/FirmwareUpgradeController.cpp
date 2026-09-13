#include "FirmwareUpgradeController.h"

#include <QtEndian>
#include <QUuid>

namespace {
constexpr int BeginResponseTimeoutMs = 20000;
constexpr quint8 FirmwareInfoInvalidDetail = 0x40U;

QString deviceErrorText(const App1Frame &response)
{
    const int result = response.payload.isEmpty()
        ? -1 : quint8(response.payload.at(0));
    if (response.payload.size() < 2)
        return QStringLiteral("error %1").arg(result);
    const quint8 detail = quint8(response.payload.at(1));
    QString meaning = QStringLiteral("unknown detail");
    if (response.type == App1Codec::BlBegin) {
        switch (detail) {
        case 0x40U: meaning = QStringLiteral("firmware information invalid"); break;
        case 0x41U: meaning = QStringLiteral("product ID mismatch"); break;
        case 0x42U: meaning = QStringLiteral("hardware revision incompatible"); break;
        case 0x43U: meaning = QStringLiteral("image base mismatch"); break;
        case 0x44U: meaning = QStringLiteral("vector base mismatch"); break;
        case 0x45U: meaning = QStringLiteral("image range invalid"); break;
        case 0x46U: meaning = QStringLiteral("stored header mismatch"); break;
        case 0x47U: meaning = QStringLiteral("device identity invalid"); break;
        default: break;
        }
    } else if (detail == 0x10U) {
        meaning = QStringLiteral("Sector 11 erase failed");
    } else if (detail >= 0x30U && detail <= 0x37U) {
        meaning = QStringLiteral("state word %1 program failed")
            .arg(detail - 0x30U);
    }
    return QStringLiteral("error %1, detail 0x%2: %3")
        .arg(result).arg(detail, 2, 16, QLatin1Char('0')).arg(meaning);
}
}

FirmwareUpgradeController::FirmwareUpgradeController(
    UpgradeTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport)
{
    Q_ASSERT(m_transport);
    qRegisterMetaType<DeviceInfo>();
    qRegisterMetaType<FirmwareInfo>();
    m_requestTimer.setSingleShot(true);
    m_requestTimer.setInterval(1000);
    m_transitionTimer.setInterval(250);
    m_recoveryTimer.setInterval(250);
    connect(&m_requestTimer, &QTimer::timeout,
            this, &FirmwareUpgradeController::onRequestTimeout);
    connect(&m_transitionTimer, &QTimer::timeout,
            this, &FirmwareUpgradeController::scanForTransition);
    connect(&m_recoveryTimer, &QTimer::timeout,
            this, &FirmwareUpgradeController::pollRecovery);
    connect(m_transport, &UpgradeTransport::dataReceived,
            this, &FirmwareUpgradeController::onDataReceived);
    connect(m_transport, &UpgradeTransport::transportError,
            this, &FirmwareUpgradeController::onTransportError);
    connect(m_transport, &UpgradeTransport::disconnected,
            this, &FirmwareUpgradeController::onDisconnected);
}

bool FirmwareUpgradeController::isActive() const
{
    return m_probePurpose != NoProbe
        || (m_stage != Idle && m_stage != Completed
            && m_stage != Failed && m_stage != Cancelled);
}

void FirmwareUpgradeController::setRequestTimeout(int milliseconds)
{
    m_requestTimeoutMs = qMax(50, milliseconds);
}

bool FirmwareUpgradeController::checkCompatibility(
    const FirmwareInfo &candidate, const DeviceInfo &device,
    const FirmwareInfo *installed, bool allowDowngrade,
    QString *description, bool *downgrade)
{
    if (downgrade)
        *downgrade = false;
    if (candidate.productId != device.productId) {
        if (description)
            *description = QStringLiteral("产品 ID 不匹配");
        return false;
    }
    if (device.hardwareRevision < candidate.hardwareRevisionMinimum
        || device.hardwareRevision > candidate.hardwareRevisionMaximum) {
        if (description)
            *description = QStringLiteral("硬件版本不在固件兼容范围内");
        return false;
    }
    if (installed) {
        const int order = FirmwareInfo::compareSemanticVersion(
            candidate, *installed);
        if (order < 0) {
            if (downgrade)
                *downgrade = true;
            if (!allowDowngrade) {
                if (description)
                    *description = QStringLiteral("候选版本较低，默认禁止降级");
                return false;
            }
            if (description)
                *description = QStringLiteral("已明确允许固件降级");
            return true;
        }
        if (order == 0) {
            if (description) {
                *description = candidate.gitCommit == installed->gitCommit
                    ? QStringLiteral("相同版本，可重新安装")
                    : QStringLiteral("相同版本的不同构建，可重新安装");
            }
            return true;
        }
    }
    if (description)
        *description = QStringLiteral("产品与硬件兼容，可以升级");
    return true;
}

void FirmwareUpgradeController::refreshDevices()
{
    if (isActive())
        return;
    QString error;
    m_refreshCandidates = m_transport->discover(&error);
    if (!error.isEmpty())
        emit logMessage(error);
    m_refreshResolved.clear();
    m_refreshIndex = 0;
    if (m_refreshCandidates.isEmpty()) {
        emit devicesChanged({});
        return;
    }
    probeNextRefreshDevice();
}

void FirmwareUpgradeController::startUpgrade(const UpgradeDevice &device,
                                             const FirmwareImage &image)
{
    if (isActive())
        return;
    if (image.image.size() <= FirmwareInfo::HeaderSize
        || image.image.size() > int(IntelHexParser::ApplicationSize)
        || image.firmwareInfo.raw.size() != FirmwareInfo::HeaderSize) {
        fail(QStringLiteral("Firmware image or information header is invalid."));
        return;
    }
    m_initialDevice = device;
    m_serial = device.serial;
    m_image = image;
    m_rxStream.clear();
    m_pendingFrame.clear();
    m_packageId = QUuid::createUuid().toRfc4122();
    m_sequence = 0U;
    m_offset = 0U;
    m_sentEnd = 0U;
    m_blockSize = 240U;
    m_retryCount = 0;
    m_installedFirmwareValid = false;
    setStage(QueryMode, QStringLiteral("Querying device mode"));
    (void)beginModeProbe(device, StartProbe, true);
}

void FirmwareUpgradeController::cancel()
{
    if (!isActive())
        return;
    if (m_stage == BeginStaging || m_stage == TransferToExternal
        || m_stage == EndStaging) {
        const QByteArray abort = App1Codec::encodeRequest(
            App1Codec::BlAbort, ++m_sequence);
        QString ignored;
        (void)m_transport->write(abort, &ignored);
    }
    m_requestTimer.stop();
    m_transitionTimer.stop();
    m_recoveryTimer.stop();
    m_transport->close();
    m_probePurpose = NoProbe;
    setStage(Cancelled, QStringLiteral("Upgrade cancelled"));
    emit finished(false, QStringLiteral("Upgrade cancelled."));
}

void FirmwareUpgradeController::setStage(Stage stage,
                                         const QString &description)
{
    m_stage = stage;
    emit stageChanged(stage, description);
    emit logMessage(description);
}

void FirmwareUpgradeController::fail(const QString &message)
{
    m_requestTimer.stop();
    m_transitionTimer.stop();
    m_probePurpose = NoProbe;
    m_transport->close();
    setStage(Failed, message);
    emit finished(false, message);
}

bool FirmwareUpgradeController::beginModeProbe(
    const UpgradeDevice &device, ProbePurpose purpose, bool fatalOpenError)
{
    m_transport->close();
    QString error;
    if (!m_transport->open(device, &error)) {
        const QString message = error.isEmpty()
            ? QStringLiteral("Unable to open device for mode query.") : error;
        if (fatalOpenError)
            fail(message);
        else
            emit logMessage(message);
        return false;
    }
    m_probeDevice = device;
    m_probeDevice.mode = UpgradeDevice::UnknownMode;
    m_probeDevice.capabilities = 0U;
    m_probePurpose = purpose;
    m_rxStream.clear();
    if (!writeRequest(App1Codec::GetMode, {}, &error)) {
        m_transport->close();
        m_probePurpose = NoProbe;
        const QString message = error.isEmpty()
            ? QStringLiteral("Unable to send GET_MODE.") : error;
        if (fatalOpenError)
            fail(message);
        else
            emit logMessage(message);
        return false;
    }
    return true;
}

void FirmwareUpgradeController::probeNextRefreshDevice()
{
    while (m_refreshIndex < m_refreshCandidates.size()) {
        const UpgradeDevice candidate = m_refreshCandidates.at(m_refreshIndex++);
        if (beginModeProbe(candidate, RefreshProbe, false))
            return;
    }
    m_probePurpose = NoProbe;
    emit devicesChanged(m_refreshResolved);
}

void FirmwareUpgradeController::handleProbeFailure(const QString &message)
{
    const ProbePurpose purpose = m_probePurpose;
    m_requestTimer.stop();
    m_probePurpose = NoProbe;
    m_transport->close();
    if (purpose == RefreshProbe) {
        emit logMessage(message);
        probeNextRefreshDevice();
    } else if (purpose == WaitBootloaderProbe
               || purpose == WaitApplicationProbe) {
        emit logMessage(message);
        QTimer::singleShot(0, this,
                           &FirmwareUpgradeController::scanForTransition);
    } else {
        fail(message);
    }
}

bool FirmwareUpgradeController::applyModeResponse(
    const QByteArray &payload, UpgradeDevice *device, QString *error) const
{
    if (!device || payload.size() != 8) {
        if (error)
            *error = QStringLiteral("GET_MODE response length is invalid.");
        return false;
    }
    if (read16(payload, 0) != App1Codec::ModeProtocolVersion
        || quint8(payload.at(3)) != 0U) {
        if (error)
            *error = QStringLiteral("GET_MODE protocol version is invalid.");
        return false;
    }
    const quint8 mode = quint8(payload.at(2));
    const quint32 capabilities = read32(payload, 4);
    quint32 required = App1Codec::CanQueryDeviceInfo
                     | App1Codec::CanQueryFirmwareInfo;
    if (mode == App1Codec::ApplicationMode) {
        required |= App1Codec::CanEnterBootloader;
        device->mode = UpgradeDevice::ApplicationMode;
    } else if (mode == App1Codec::BootloaderMode) {
        required |= App1Codec::CanUpgrade | App1Codec::CanReboot;
        device->mode = UpgradeDevice::BootloaderMode;
    } else {
        if (error)
            *error = QStringLiteral("GET_MODE mode is invalid.");
        return false;
    }
    if ((capabilities & required) != required) {
        if (error)
            *error = QStringLiteral("Device does not support information queries.");
        return false;
    }
    device->capabilities = capabilities;
    if (error)
        error->clear();
    return true;
}

void FirmwareUpgradeController::sendRequest(quint16 type,
                                            const QByteArray &payload)
{
    QString error;
    if (!writeRequest(type, payload, &error))
        fail(error.isEmpty() ? QStringLiteral("USB write failed.") : error);
}

bool FirmwareUpgradeController::writeRequest(quint16 type,
                                             const QByteArray &payload,
                                             QString *error)
{
    m_pendingType = type;
    m_pendingSequence = ++m_sequence;
    m_pendingFrame = App1Codec::encodeRequest(type, m_pendingSequence, payload);
    if (m_pendingFrame.isEmpty()
        || !m_transport->write(m_pendingFrame, error))
        return false;
    m_retryCount = 0;
    m_requestTimer.setInterval(
        type == App1Codec::BlBegin
            ? qMax(BeginResponseTimeoutMs, m_requestTimeoutMs)
            : m_requestTimeoutMs);
    m_requestTimer.start();
    return true;
}

void FirmwareUpgradeController::finishInformationProbe(bool installedValid)
{
    m_installedFirmwareValid = installedValid;
    QString compatibility;
    bool downgrade = false;
    const bool allowed = checkCompatibility(
        m_image.firmwareInfo, m_deviceInfo,
        installedValid ? &m_installedFirmware : nullptr,
        m_allowDowngrade, &compatibility, &downgrade);
    emit deviceInformationChanged(m_deviceInfo, m_installedFirmware,
                                  installedValid, compatibility,
                                  allowed, downgrade);
    const ProbePurpose purpose = m_probePurpose;
    m_probePurpose = NoProbe;

    if (purpose == WaitApplicationProbe) {
        m_transport->close();
        if (m_probeDevice.mode != UpgradeDevice::ApplicationMode) {
            return;
        }
        if (!installedValid
            || !m_image.firmwareInfo.sameBuildIdentity(m_installedFirmware)) {
            fail(QStringLiteral(
                "Post-upgrade firmware identity verification failed."));
            return;
        }
        m_transitionTimer.stop();
        setStage(Completed, QStringLiteral("Firmware upgrade completed"));
        emit finished(true, QStringLiteral(
            "Firmware upgrade and version verification completed."));
        return;
    }
    if (!allowed) {
        fail(QStringLiteral("Compatibility check failed: %1")
             .arg(compatibility));
        return;
    }
    if (m_image.firmwareInfo.isDebugBuild())
        emit logMessage(QStringLiteral("Warning: candidate is a Debug build."));
    if (m_image.firmwareInfo.isDirty())
        emit logMessage(QStringLiteral("Warning: candidate was built from a dirty tree."));

    if (m_probeDevice.mode == UpgradeDevice::ApplicationMode) {
        setStage(EnterBootloader, QStringLiteral("Requesting Bootloader mode"));
        sendRequest(App1Codec::EnterBootloader);
    } else if (purpose == StartProbe || purpose == WaitBootloaderProbe) {
        m_transitionTimer.stop();
        sendHello();
    } else {
        fail(QStringLiteral("Unexpected information-query state."));
    }
}

void FirmwareUpgradeController::sendHello()
{
    setStage(Hello, QStringLiteral("Reading Bootloader capabilities"));
    sendRequest(App1Codec::BlHello);
}

void FirmwareUpgradeController::sendStatus()
{
    sendRequest(App1Codec::BlStatus);
}

void FirmwareUpgradeController::sendBegin()
{
    QByteArray payload;
    append16(payload, 3U);
    append16(payload, FirmwareInfo::HeaderSize);
    append32(payload, m_image.baseAddress);
    append32(payload, quint32(m_image.image.size()));
    append32(payload, m_image.crc32);
    payload.append(m_packageId);
    payload.append(m_image.firmwareInfo.raw);
    setStage(BeginStaging, QStringLiteral("Writing candidate to W25Q128"));
    sendRequest(App1Codec::BlBegin, payload);
}

void FirmwareUpgradeController::sendNextData()
{
    if (m_offset >= quint32(m_image.image.size())) {
        sendEnd();
        return;
    }
    const quint32 count = qMin<quint32>(
        qMin<quint32>(m_blockSize, quint32(m_image.image.size()) - m_offset),
        4096U - (m_offset & 4095U));
    QByteArray payload;
    append32(payload, m_offset);
    append16(payload, quint16(count));
    payload.append(m_image.image.mid(int(m_offset), int(count)));
    m_sentEnd = m_offset + count;
    setStage(TransferToExternal, QStringLiteral("Writing candidate to W25Q128"));
    sendRequest(App1Codec::BlData, payload);
}

void FirmwareUpgradeController::sendEnd()
{
    setStage(EndStaging, QStringLiteral("Verifying staged W25Q128 image"));
    sendRequest(App1Codec::BlEnd);
}

void FirmwareUpgradeController::sendInstall()
{
    setStage(Install, QStringLiteral("Starting STM32 internal Flash installation"));
    sendRequest(App1Codec::BlInstall);
}

void FirmwareUpgradeController::onDataReceived(const QByteArray &data)
{
    m_rxStream.append(data);
    App1Frame response;
    QString error;
    while (App1Codec::takeFrame(&m_rxStream, &response, &error))
        handleResponse(response);
}

void FirmwareUpgradeController::handleResponse(const App1Frame &response)
{
    if ((response.flags & App1Codec::ResponseFlag) == 0U
        || response.type != m_pendingType
        || response.sequence != m_pendingSequence) {
        emit logMessage(QStringLiteral("Ignored unmatched APP1 response."));
        return;
    }
    m_requestTimer.stop();

    if (response.type == App1Codec::GetMode) {
        if ((response.flags & App1Codec::ErrorFlag) != 0U) {
            handleProbeFailure(QStringLiteral("Device rejected GET_MODE."));
            return;
        }
        UpgradeDevice resolved = m_probeDevice;
        QString error;
        if (!applyModeResponse(response.payload, &resolved, &error)) {
            handleProbeFailure(error);
            return;
        }
        m_probeDevice = resolved;
        if (m_probePurpose == RefreshProbe) {
            m_probePurpose = NoProbe;
            m_transport->close();
            m_refreshResolved.append(resolved);
            probeNextRefreshDevice();
            return;
        }
        if ((m_probePurpose == WaitBootloaderProbe
             && !resolved.isBootloader())
            || (m_probePurpose == WaitApplicationProbe
                && !resolved.isApplication())) {
            m_probePurpose = NoProbe;
            m_transport->close();
            return;
        }
        setStage(QueryDeviceInformation,
                 QStringLiteral("Reading device identity"));
        sendRequest(App1Codec::GetDeviceInfo);
        return;
    }

    if (response.type == App1Codec::GetDeviceInfo) {
        if ((response.flags & App1Codec::ErrorFlag) != 0U) {
            handleProbeFailure(QStringLiteral("Device rejected GET_DEVICE_INFO."));
            return;
        }
        QString error;
        if (!DeviceInfo::decode(response.payload, &m_deviceInfo, &error)
            || m_deviceInfo.mode != (m_probeDevice.isApplication()
                                     ? App1Codec::ApplicationMode
                                     : App1Codec::BootloaderMode)) {
            handleProbeFailure(error.isEmpty()
                ? QStringLiteral("Device mode responses disagree.") : error);
            return;
        }
        setStage(QueryFirmwareInformation,
                 QStringLiteral("Reading installed firmware information"));
        sendRequest(App1Codec::GetFirmwareInfo);
        return;
    }

    if (response.type == App1Codec::GetFirmwareInfo) {
        if ((response.flags & App1Codec::ErrorFlag) != 0U) {
            const bool absent = m_probeDevice.isBootloader()
                && response.payload.size() >= 2
                && quint8(response.payload.at(1)) == FirmwareInfoInvalidDetail;
            if (!absent) {
                handleProbeFailure(
                    QStringLiteral("Device rejected GET_FIRMWARE_INFO."));
                return;
            }
            m_installedFirmware = {};
            finishInformationProbe(false);
            return;
        }
        QString error;
        if (!FirmwareInfo::decode(response.payload,
                                  &m_installedFirmware, &error)) {
            handleProbeFailure(error);
            return;
        }
        finishInformationProbe(true);
        return;
    }

    if ((response.flags & App1Codec::ErrorFlag) != 0U) {
        fail(QStringLiteral("Device rejected command 0x%1 (%2).")
             .arg(response.type, 4, 16, QLatin1Char('0'))
             .arg(deviceErrorText(response)));
        return;
    }

    switch (response.type) {
    case App1Codec::EnterBootloader:
        if (response.payload != QByteArrayLiteral("OK,REBOOTING")) {
            fail(QStringLiteral(
                "Application reboot acknowledgement is malformed."));
            return;
        }
        m_transport->close();
        setStage(WaitBootloader,
                 QStringLiteral("Waiting for Bootloader USB device"));
        m_transitionElapsed.restart();
        m_transitionTimer.start();
        QTimer::singleShot(0, this,
                           &FirmwareUpgradeController::scanForTransition);
        break;
    case App1Codec::BlHello:
        if (response.payload.size() < 20
            || read16(response.payload, 0) != 2U
            || read32(response.payload, 4) != IntelHexParser::ApplicationBase
            || read32(response.payload, 8) < quint32(m_image.image.size())) {
            fail(QStringLiteral("Bootloader capabilities are incompatible."));
            return;
        }
        m_blockSize = qMin<quint16>(240U, read16(response.payload, 12));
        if (m_blockSize == 0U) {
            fail(QStringLiteral("Bootloader reported an invalid block size."));
            return;
        }
        sendStatus();
        break;
    case App1Codec::BlStatus: {
        if (response.payload.size() != 48 || read16(response.payload, 0) != 2U) {
            fail(QStringLiteral("Bootloader V2 status response is malformed."));
            return;
        }
        const quint8 phase = quint8(response.payload.at(2));
        const quint32 size = read32(response.payload, 8);
        const quint32 crc = read32(response.payload, 12);
        const QByteArray packageId = response.payload.mid(32, 16);
        emit recoveryStatusChanged(phase, quint8(response.payload.at(4)),
                                   quint8(response.payload.at(5)), packageId,
                                   read32(response.payload, 16), read32(response.payload, 20),
                                   read32(response.payload, 24), read16(response.payload, 28));
        if (phase == 2U && (size != quint32(m_image.image.size())
                            || crc != m_image.crc32 || packageId != m_packageId)) {
            setStage(BeginStaging,
                     QStringLiteral("Discarding a different staged image"));
            sendRequest(App1Codec::BlAbort);
        } else if ((phase >= 4U && phase <= 8U)
                   || (phase == 3U && m_stage == MonitorRecovery)) {
            setStage(MonitorRecovery,
                     QStringLiteral("Installing firmware into STM32 internal Flash"));
            if (!m_recoveryTimer.isActive())
                m_recoveryTimer.start();
        } else if (phase == 1U && m_stage == MonitorRecovery) {
            m_recoveryTimer.stop();
            m_transport->close();
            setStage(WaitApplication, QStringLiteral("Waiting for upgraded application"));
            m_transitionElapsed.restart();
            m_transitionTimer.start();
            QTimer::singleShot(0, this, &FirmwareUpgradeController::scanForTransition);
        } else if (phase == 9U) {
            fail(QStringLiteral("Bootloader recovery reported an error."));
        } else {
            sendBegin();
        }
        break;
    }
    case App1Codec::BlAbort:
        sendBegin();
        break;
    case App1Codec::BlBegin:
        if (response.payload.size() != 4) {
            fail(QStringLiteral("BL_BEGIN response is malformed."));
            return;
        }
        m_offset = read32(response.payload, 0);
        if (m_offset < FirmwareInfo::HeaderSize
            || m_offset > quint32(m_image.image.size())) {
            fail(QStringLiteral("Bootloader resume offset is outside the image."));
            return;
        }
        m_transferStartOffset = m_offset;
        m_transferElapsed.restart();
        updateProgress(m_offset);
        sendNextData();
        break;
    case App1Codec::BlData: {
        if (response.payload.size() != 4) {
            fail(QStringLiteral("BL_DATA response is malformed."));
            return;
        }
        const quint32 acknowledged = read32(response.payload, 0);
        if (acknowledged < m_offset || acknowledged > m_sentEnd) {
            fail(QStringLiteral("Bootloader acknowledged an invalid offset."));
            return;
        }
        if (acknowledged > m_offset) {
            m_offset = acknowledged;
            updateProgress(m_offset);
        }
        sendNextData();
        break;
    }
    case App1Codec::BlEnd:
        sendInstall();
        break;
    case App1Codec::BlInstall:
        setStage(MonitorRecovery,
                 QStringLiteral("Installing firmware into STM32 internal Flash"));
        m_recoveryTimer.start();
        break;
    default:
        fail(QStringLiteral("Unexpected Bootloader response."));
        break;
    }
}

void FirmwareUpgradeController::updateProgress(quint32 acknowledged)
{
    const qint64 elapsedMs = qMax<qint64>(1, m_transferElapsed.elapsed());
    const quint32 sessionBytes = acknowledged - m_transferStartOffset;
    const double speed = double(sessionBytes) * 1000.0 / double(elapsedMs);
    const quint32 remaining = quint32(m_image.image.size()) - acknowledged;
    const int eta = speed > 0.0 ? int(double(remaining) / speed + 0.5) : 0;
    emit progressChanged(acknowledged, quint32(m_image.image.size()), speed, eta);
}

void FirmwareUpgradeController::pollRecovery()
{
    if (m_stage == MonitorRecovery && !m_requestTimer.isActive())
        sendStatus();
}

void FirmwareUpgradeController::onTransportError(const QString &message)
{
    if (m_probePurpose != NoProbe)
        handleProbeFailure(message);
    else if (isActive())
        fail(message);
}

void FirmwareUpgradeController::onDisconnected()
{
    m_requestTimer.stop();
    if (m_probePurpose != NoProbe) {
        handleProbeFailure(QStringLiteral(
            "Device disconnected during information query."));
        return;
    }
    if (m_stage == EnterBootloader) {
        setStage(WaitBootloader,
                 QStringLiteral("Waiting for Bootloader USB device"));
    } else if (m_stage == EndStaging || m_stage == MonitorRecovery) {
        m_recoveryTimer.stop();
        setStage(WaitApplication,
                 QStringLiteral("Waiting for upgraded application"));
    } else if (m_stage == WaitBootloader || m_stage == WaitApplication) {
        return;
    } else {
        fail(QStringLiteral("WinUSB device disconnected during upgrade."));
        return;
    }
    m_transitionElapsed.restart();
    m_transitionTimer.start();
    QTimer::singleShot(0, this,
                       &FirmwareUpgradeController::scanForTransition);
}

void FirmwareUpgradeController::onRequestTimeout()
{
    if (++m_retryCount > 3) {
        if (m_probePurpose != NoProbe)
            handleProbeFailure(QStringLiteral("Device query response timeout."));
        else
            fail(QStringLiteral("USB response timeout."));
        return;
    }
    QString error;
    emit logMessage(QStringLiteral("Response timeout, retransmitting command."));
    if (!m_transport->write(m_pendingFrame, &error)) {
        fail(error.isEmpty()
             ? QStringLiteral("USB retransmission failed.") : error);
        return;
    }
    m_requestTimer.start();
}

void FirmwareUpgradeController::scanForTransition()
{
    if (m_probePurpose != NoProbe)
        return;
    if (m_transitionElapsed.elapsed() > 10000) {
        fail(QStringLiteral("Timed out waiting for USB re-enumeration."));
        return;
    }
    QString error;
    const QList<UpgradeDevice> devices = m_transport->discover(&error);
    if (!error.isEmpty())
        emit logMessage(error);
    for (const UpgradeDevice &device : devices) {
        if (m_serial.isEmpty() || device.serial == m_serial) {
            const ProbePurpose purpose = m_stage == WaitBootloader
                ? WaitBootloaderProbe : WaitApplicationProbe;
            (void)beginModeProbe(device, purpose, false);
            return;
        }
    }
}

quint16 FirmwareUpgradeController::read16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 FirmwareUpgradeController::read32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

void FirmwareUpgradeController::append16(QByteArray &data, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes));
    data.append(bytes, 2);
}

void FirmwareUpgradeController::append32(QByteArray &data, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes));
    data.append(bytes, 4);
}
