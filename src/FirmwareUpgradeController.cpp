#include "FirmwareUpgradeController.h"

#include <QtEndian>

namespace {
constexpr int BeginResponseTimeoutMs = 20000;

QString storageErrorText(quint8 detail)
{
    if (detail == 0x01U)
        return QStringLiteral("state storage argument or port invalid");
    if (detail == 0x10U)
        return QStringLiteral("Sector 11 erase failed");
    if (detail == 0x20U)
        return QStringLiteral("state storage capacity exhausted");
    if (detail >= 0x30U && detail <= 0x37U)
        return QStringLiteral("state word %1 program failed").arg(detail - 0x30U);
    if (detail >= 0x40U && detail <= 0x47U)
        return QStringLiteral("state word %1 readback mismatch").arg(detail - 0x40U);
    return QStringLiteral("unknown storage failure");
}

QString deviceErrorText(const App1Frame &response)
{
    if (response.type == App1Codec::EnterBootloader)
        return QString::fromLatin1(response.payload);

    const int result = response.payload.isEmpty()
        ? -1 : quint8(response.payload.at(0));
    if (result != 5 || response.payload.size() < 2) {
        return QStringLiteral("error %1").arg(result);
    }

    const quint8 storageDetail = quint8(response.payload.at(1));
    if (storageDetail == 0U)
        return QStringLiteral("error %1").arg(result);
    return QStringLiteral("error %1, storage detail 0x%2: %3")
        .arg(result)
        .arg(storageDetail, 2, 16, QLatin1Char('0'))
        .arg(storageErrorText(storageDetail));
}
}

FirmwareUpgradeController::FirmwareUpgradeController(
    UpgradeTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport)
{
    Q_ASSERT(m_transport);
    m_requestTimer.setSingleShot(true);
    m_requestTimer.setInterval(1000);
    m_transitionTimer.setInterval(250);
    connect(&m_requestTimer, &QTimer::timeout,
            this, &FirmwareUpgradeController::onRequestTimeout);
    connect(&m_transitionTimer, &QTimer::timeout,
            this, &FirmwareUpgradeController::scanForTransition);
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
    if (image.image.isEmpty()
        || image.image.size() > int(IntelHexParser::ApplicationSize)) {
        fail(QStringLiteral("Firmware image is empty or too large."));
        return;
    }
    m_initialDevice = device;
    m_serial = device.serial;
    m_image = image;
    m_rxStream.clear();
    m_pendingFrame.clear();
    m_sequence = 0U;
    m_offset = 0U;
    m_sentEnd = 0U;
    m_blockSize = 240U;
    m_retryCount = 0;
    setStage(QueryMode, QStringLiteral("Querying device mode"));
    (void)beginModeProbe(device, StartProbe, true);
}

void FirmwareUpgradeController::cancel()
{
    if (!isActive())
        return;
    if (m_stage == Begin || m_stage == Transfer || m_stage == End) {
        const QByteArray abort = App1Codec::encodeRequest(
            App1Codec::BlAbort, ++m_sequence);
        QString ignored;
        (void)m_transport->write(abort, &ignored);
    }
    m_requestTimer.stop();
    m_transitionTimer.stop();
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

bool FirmwareUpgradeController::openDevice(const UpgradeDevice &device)
{
    QString error;
    if (!m_transport->open(device, &error)) {
        fail(error.isEmpty() ? QStringLiteral("Unable to open device.") : error);
        return false;
    }
    return true;
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
        const UpgradeDevice candidate =
            m_refreshCandidates.at(m_refreshIndex++);
        if (beginModeProbe(candidate, RefreshProbe, false))
            return;
    }
    m_probePurpose = NoProbe;
    emit devicesChanged(m_refreshResolved);
}

void FirmwareUpgradeController::handleModeProbeFailure(
    const QString &message)
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
    if (mode == App1Codec::ApplicationMode
        && capabilities == App1Codec::CanEnterBootloader) {
        device->mode = UpgradeDevice::ApplicationMode;
    } else if (mode == App1Codec::BootloaderMode
               && capabilities
                      == (App1Codec::CanUpgrade | App1Codec::CanReboot)) {
        device->mode = UpgradeDevice::BootloaderMode;
    } else {
        if (error)
            *error = QStringLiteral("GET_MODE mode or capabilities are invalid.");
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
    m_pendingFrame = App1Codec::encodeRequest(
        type, m_pendingSequence, payload);
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
    append32(payload, quint32(m_image.image.size()));
    append32(payload, m_image.crc32);
    append32(payload, m_image.imageVersion);
    setStage(Begin, QStringLiteral("Starting firmware transaction"));
    sendRequest(App1Codec::BlBegin, payload);
}

void FirmwareUpgradeController::sendNextData()
{
    if (m_offset >= quint32(m_image.image.size())) {
        sendEnd();
        return;
    }
    const quint32 count = qMin<quint32>(
        m_blockSize, quint32(m_image.image.size()) - m_offset);
    QByteArray payload;
    append32(payload, m_offset);
    append16(payload, quint16(count));
    payload.append(m_image.image.mid(int(m_offset), int(count)));
    m_sentEnd = m_offset + count;
    setStage(Transfer, QStringLiteral("Transferring firmware"));
    sendRequest(App1Codec::BlData, payload);
}

void FirmwareUpgradeController::sendEnd()
{
    QByteArray payload;
    append32(payload, quint32(m_image.image.size()));
    append32(payload, m_image.crc32);
    setStage(End, QStringLiteral("Verifying firmware"));
    sendRequest(App1Codec::BlEnd, payload);
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
            handleModeProbeFailure(
                QStringLiteral("Device rejected GET_MODE."));
            return;
        }
        UpgradeDevice resolved = m_probeDevice;
        QString error;
        if (!applyModeResponse(response.payload, &resolved, &error)) {
            handleModeProbeFailure(error);
            return;
        }

        const ProbePurpose purpose = m_probePurpose;
        m_probePurpose = NoProbe;
        if (purpose == RefreshProbe) {
            m_transport->close();
            m_refreshResolved.append(resolved);
            probeNextRefreshDevice();
        } else if (purpose == StartProbe) {
            m_initialDevice = resolved;
            m_serial = resolved.serial;
            if (resolved.mode == UpgradeDevice::ApplicationMode) {
                setStage(EnterBootloader,
                         QStringLiteral("Requesting Bootloader mode"));
                sendRequest(App1Codec::EnterBootloader);
            } else {
                sendHello();
            }
        } else if (purpose == WaitBootloaderProbe) {
            if (resolved.mode == UpgradeDevice::BootloaderMode) {
                m_transitionTimer.stop();
                sendHello();
            } else {
                m_transport->close();
                QTimer::singleShot(
                    0, this, &FirmwareUpgradeController::scanForTransition);
            }
        } else if (purpose == WaitApplicationProbe) {
            m_transport->close();
            if (resolved.mode == UpgradeDevice::ApplicationMode) {
                m_transitionTimer.stop();
                setStage(Completed,
                         QStringLiteral("Firmware upgrade completed"));
                emit finished(
                    true, QStringLiteral("Firmware upgrade completed."));
            } else {
                QTimer::singleShot(
                    0, this, &FirmwareUpgradeController::scanForTransition);
            }
        } else {
            fail(QStringLiteral("Unexpected GET_MODE response."));
        }
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
        setStage(WaitBootloader, QStringLiteral("Waiting for Bootloader USB device"));
        m_transitionElapsed.restart();
        m_transitionTimer.start();
        QTimer::singleShot(0, this, &FirmwareUpgradeController::scanForTransition);
        break;
    case App1Codec::BlHello:
        if (response.payload.size() < 20
            || read16(response.payload, 0) != 1U
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
        if (response.payload.size() != 14) {
            fail(QStringLiteral("Bootloader status response is malformed."));
            return;
        }
        const quint32 size = read32(response.payload, 0);
        const quint32 crc = read32(response.payload, 4);
        const quint16 state = read16(response.payload, 12);
        if (state == 1U
            && (size != quint32(m_image.image.size()) || crc != m_image.crc32)) {
            setStage(Begin, QStringLiteral("Discarding a different interrupted image"));
            sendRequest(App1Codec::BlAbort);
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
        if (m_offset > quint32(m_image.image.size())) {
            fail(QStringLiteral("Bootloader resume offset is outside the image."));
            return;
        }
        m_transferStartOffset = m_offset;
        m_transferElapsed.restart();
        if (m_offset != 0U)
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
        m_transport->close();
        setStage(WaitApplication, QStringLiteral("Waiting for upgraded application"));
        m_transitionElapsed.restart();
        m_transitionTimer.start();
        QTimer::singleShot(0, this, &FirmwareUpgradeController::scanForTransition);
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

void FirmwareUpgradeController::onTransportError(const QString &message)
{
    if (m_probePurpose != NoProbe)
        handleModeProbeFailure(message);
    else if (isActive())
        fail(message);
}

void FirmwareUpgradeController::onDisconnected()
{
    m_requestTimer.stop();
    if (m_probePurpose != NoProbe) {
        handleModeProbeFailure(
            QStringLiteral("Device disconnected during GET_MODE."));
        return;
    }
    if (m_stage == EnterBootloader) {
        setStage(WaitBootloader,
                 QStringLiteral("Waiting for Bootloader USB device"));
    } else if (m_stage == End) {
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
    QTimer::singleShot(0, this, &FirmwareUpgradeController::scanForTransition);
}

void FirmwareUpgradeController::onRequestTimeout()
{
    if (++m_retryCount > 3) {
        if (m_pendingType == App1Codec::GetMode
            && m_probePurpose != NoProbe) {
            handleModeProbeFailure(
                QStringLiteral("GET_MODE response timeout."));
        } else {
            fail(QStringLiteral("USB response timeout."));
        }
        return;
    }
    QString error;
    emit logMessage(QStringLiteral("Response timeout, retransmitting command."));
    if (!m_transport->write(m_pendingFrame, &error)) {
        fail(error.isEmpty() ? QStringLiteral("USB retransmission failed.") : error);
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
