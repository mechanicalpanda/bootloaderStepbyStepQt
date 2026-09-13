#include <QtTest>

#include "App1Codec.h"
#include "DeviceInfo.h"
#include "FirmwareInfo.h"
#include "FirmwareUpgradeController.h"
#include "FirmwareUpgradeDialog.h"
#include "IntelHexParser.h"
#include "UpgradeTransport.h"
#include "WinUsbDeviceDiscovery.h"
#include "WinUsbTransport.h"

#include <QtEndian>

class FakeTransport : public UpgradeTransport
{
public:
    QList<UpgradeDevice> devices;
    QList<QByteArray> writes;
    bool opened = false;

    QList<UpgradeDevice> discover(QString *error) override
    {
        if (error)
            error->clear();
        return devices;
    }

    bool open(const UpgradeDevice &, QString *error) override
    {
        opened = true;
        if (error)
            error->clear();
        return true;
    }

    void close() override
    {
        opened = false;
    }

    bool write(const QByteArray &data, QString *error) override
    {
        writes.append(data);
        if (error)
            error->clear();
        return true;
    }

    void deliver(const QByteArray &data)
    {
        emit dataReceived(data);
    }

    void disconnectNow()
    {
        opened = false;
        emit disconnected();
    }
};

class FirmwareCoreTest : public QObject
{
    Q_OBJECT

private:
    static void write16(QByteArray &data, int offset, quint16 value)
    {
        qToLittleEndian(value,
                        reinterpret_cast<uchar *>(data.data() + offset));
    }

    static void write32(QByteArray &data, int offset, quint32 value)
    {
        qToLittleEndian(value,
                        reinterpret_cast<uchar *>(data.data() + offset));
    }

    static void write64(QByteArray &data, int offset, quint64 value)
    {
        qToLittleEndian(value,
                        reinterpret_cast<uchar *>(data.data() + offset));
    }

    static void append16(QByteArray &data, quint16 value)
    {
        char bytes[2];
        qToLittleEndian(value, reinterpret_cast<uchar *>(bytes));
        data.append(bytes, 2);
    }

    static void append32(QByteArray &data, quint32 value)
    {
        char bytes[4];
        qToLittleEndian(value, reinterpret_cast<uchar *>(bytes));
        data.append(bytes, 4);
    }

    static QByteArray header(quint16 major = 1U, quint16 minor = 0U,
                             quint16 patch = 0U,
                             quint32 product = 0x00010001U,
                             const QByteArray &commit = "0123456789ab")
    {
        QByteArray bytes(FirmwareInfo::HeaderSize, char(0));
        bytes.replace(0, 4, QByteArrayLiteral("FWI1"));
        write16(bytes, 4, 1U);
        write16(bytes, 6, FirmwareInfo::HeaderSize);
        write32(bytes, 8, product);
        write16(bytes, 0x0C, 1U);
        write16(bytes, 0x0E, 1U);
        write16(bytes, 0x10, major);
        write16(bytes, 0x12, minor);
        write16(bytes, 0x14, patch);
        write16(bytes, 0x16, FirmwareInfo::DebugBuildFlag);
        write32(bytes, 0x18, 77U);
        write32(bytes, 0x1C, IntelHexParser::ApplicationBase);
        write32(bytes, 0x20, IntelHexParser::VectorBase);
        write32(bytes, 0x24, IntelHexParser::ApplicationSize);
        write64(bytes, 0x28, 1700000000ULL);
        bytes.replace(0x30, 5, QByteArrayLiteral("MYFOC"));
        bytes.replace(0x50, 22, QByteArrayLiteral("MYFOC Motor Controller"));
        bytes.replace(0x70, 19, QByteArrayLiteral("FOCTEST Application"));
        bytes.replace(0x90, commit.size(), commit);
        write32(bytes, 0xFC, App1Codec::crc32(bytes.left(0xFC)));
        return bytes;
    }

    static FirmwareInfo info(quint16 major = 1U, quint16 minor = 0U,
                             quint16 patch = 0U,
                             quint32 product = 0x00010001U,
                             const QByteArray &commit = "0123456789ab")
    {
        FirmwareInfo result;
        QString error;
        const bool decoded = FirmwareInfo::decode(
            header(major, minor, patch, product, commit), &result, &error);
        Q_ASSERT_X(decoded, "FirmwareCoreTest::info", qPrintable(error));
        return result;
    }

    static FirmwareImage image(const FirmwareInfo &firmwareInfo = info())
    {
        FirmwareImage result;
        result.baseAddress = IntelHexParser::ApplicationBase;
        result.image = QByteArray(600, char(0xFF));
        result.image.replace(0, FirmwareInfo::HeaderSize, firmwareInfo.raw);
        result.crc32 = App1Codec::crc32(result.image);
        result.firmwareInfo = firmwareInfo;
        return result;
    }

    static QByteArray modePayload(quint8 mode)
    {
        QByteArray payload;
        append16(payload, App1Codec::ModeProtocolVersion);
        payload.append(char(mode));
        payload.append(char(0));
        quint32 capabilities = App1Codec::CanQueryDeviceInfo
                             | App1Codec::CanQueryFirmwareInfo;
        capabilities |= mode == App1Codec::ApplicationMode
            ? App1Codec::CanEnterBootloader
            : App1Codec::CanUpgrade | App1Codec::CanReboot;
        append32(payload, capabilities);
        return payload;
    }

    static QByteArray devicePayload(quint8 mode,
                                    quint32 product = 0x00010001U,
                                    quint16 hardware = 1U)
    {
        QByteArray payload(DeviceInfo::PayloadSize, char(0));
        write16(payload, 0, 1U);
        payload[2] = char(mode);
        write32(payload, 4, product);
        write16(payload, 8, hardware);
        quint32 capabilities = App1Codec::CanQueryDeviceInfo
                             | App1Codec::CanQueryFirmwareInfo;
        capabilities |= mode == App1Codec::ApplicationMode
            ? App1Codec::CanEnterBootloader
            : App1Codec::CanUpgrade | App1Codec::CanReboot;
        write32(payload, 12, capabilities);
        payload.replace(16, 12, QByteArray::fromHex(
            "001E00453432511933373439"));
        return payload;
    }

    static QByteArray statusPayload(quint8 phase = 0U)
    {
        QByteArray payload(48, char(0));
        payload[0] = char(2);
        payload[2] = char(phase);
        return payload;
    }

    static UpgradeDevice upgradeDevice(UpgradeDevice::Mode mode)
    {
        UpgradeDevice device;
        device.path = QStringLiteral("fake-device");
        device.serial = QStringLiteral("001E00453432511933373439");
        device.product = QStringLiteral("MYFOC Motor Controller");
        device.vid = 0xCAFEU;
        device.pid = 0x4070U;
        device.mode = mode;
        return device;
    }

    static App1Frame lastRequest(const FakeTransport &transport)
    {
        App1Frame frame;
        QString error;
        const bool decoded = App1Codec::decode(
            transport.writes.last(), &frame, &error);
        Q_ASSERT_X(decoded, "FirmwareCoreTest::lastRequest", qPrintable(error));
        return frame;
    }

    static void respond(FakeTransport &transport, quint16 expectedType,
                        const QByteArray &payload, bool error = false)
    {
        const App1Frame request = lastRequest(transport);
        QCOMPARE(request.type, expectedType);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, error, payload));
    }

    static void answerInformationSequence(
        FakeTransport &transport, quint8 mode,
        const QByteArray &installedHeader = header(),
        quint32 product = 0x00010001U, quint16 hardware = 1U)
    {
        respond(transport, App1Codec::GetMode, modePayload(mode));
        respond(transport, App1Codec::GetDeviceInfo,
                devicePayload(mode, product, hardware));
        respond(transport, App1Codec::GetFirmwareInfo, installedHeader);
    }

private slots:
    void classifiesWindowsNoSuchDeviceAsDisconnect()
    {
        QVERIFY(WinUsbTransport::isDisconnectError(433UL));
        QVERIFY(!WinUsbTransport::isDisconnectError(5UL));
    }

    void exposesFirmwareAndCompatibilityControls()
    {
        FirmwareUpgradeDialog dialog;
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("deviceCombo")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("browseButton")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("candidateFirmwareInfo")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("deviceFirmwareInfo")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("compatibilityStatus")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("allowDowngrade")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("startButton")));
    }

    void parsesWinUsbDeviceIdentityFromInterfacePath()
    {
        UpgradeDevice device;
        const QString path = QStringLiteral(
            R"(\\?\usb#vid_cafe&pid_4070&mi_02#ABC123#{6e15414d-b3e8-4b08-9b73-73db7e6a0f40})");
        QVERIFY(WinUsbDeviceDiscovery::parseDevicePath(path, &device));
        QCOMPARE(device.vid, quint16(0xCAFEU));
        QCOMPARE(device.pid, quint16(0x4070U));
        QCOMPARE(device.serial, QStringLiteral("ABC123"));
    }

    void app1AcceptsAndReassembles320BytePayload()
    {
        const QByteArray payload(320, char(0xA5));
        const QByteArray encoded = App1Codec::encodeResponse(
            App1Codec::GetFirmwareInfo, 9U, false, payload);
        QVERIFY(!encoded.isEmpty());
        QByteArray stream = QByteArrayLiteral("noise") + encoded.left(63);
        App1Frame decoded;
        QString error;
        QVERIFY(!App1Codec::takeFrame(&stream, &decoded, &error));
        stream += encoded.mid(63);
        QVERIFY2(App1Codec::takeFrame(&stream, &decoded, &error),
                 qPrintable(error));
        QCOMPARE(decoded.payload, payload);
    }

    void refreshQueriesModeWithoutTrustingPid()
    {
        FakeTransport transport;
        transport.devices = {upgradeDevice(UpgradeDevice::UnknownMode)};
        FirmwareUpgradeController controller(&transport);
        QSignalSpy devicesSpy(&controller,
                              &FirmwareUpgradeController::devicesChanged);
        controller.refreshDevices();
        respond(transport, App1Codec::GetMode,
                modePayload(App1Codec::ApplicationMode));
        QCOMPARE(devicesSpy.count(), 1);
        const QList<UpgradeDevice> devices =
            qvariant_cast<QList<UpgradeDevice>>(devicesSpy.last().at(0));
        QCOMPARE(devices.first().mode, UpgradeDevice::ApplicationMode);
    }

    void queriesIdentityBeforeEnteringBootloader()
    {
        FakeTransport transport;
        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(upgradeDevice(UpgradeDevice::ApplicationMode),
                                image());
        QCOMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
        respond(transport, App1Codec::GetMode,
                modePayload(App1Codec::ApplicationMode));
        QCOMPARE(lastRequest(transport).type,
                 quint16(App1Codec::GetDeviceInfo));
        respond(transport, App1Codec::GetDeviceInfo,
                devicePayload(App1Codec::ApplicationMode));
        QCOMPARE(lastRequest(transport).type,
                 quint16(App1Codec::GetFirmwareInfo));
        respond(transport, App1Codec::GetFirmwareInfo, header());
        QCOMPARE(lastRequest(transport).type,
                 quint16(App1Codec::EnterBootloader));
    }

    void doesNotRequestBootloaderAgainWhileWaitingForIt()
    {
        FakeTransport transport;
        const UpgradeDevice app = upgradeDevice(UpgradeDevice::ApplicationMode);
        transport.devices = {app};
        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(app, image());
        answerInformationSequence(transport, App1Codec::ApplicationMode);
        respond(transport, App1Codec::EnterBootloader,
                QByteArrayLiteral("OK,REBOOTING"));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::WaitBootloader);

        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
        respond(transport, App1Codec::GetMode,
                modePayload(App1Codec::ApplicationMode));

        QCOMPARE(controller.stage(), FirmwareUpgradeController::WaitBootloader);
        QCOMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
    }

    void rejectsProductBeforeModeChange()
    {
        FakeTransport transport;
        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(upgradeDevice(UpgradeDevice::ApplicationMode),
                                image());
        answerInformationSequence(transport, App1Codec::ApplicationMode,
                                  header(), 0x00020002U);
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Failed);
        for (const QByteArray &write : transport.writes) {
            App1Frame request;
            QString error;
            QVERIFY(App1Codec::decode(write, &request, &error));
            QVERIFY(request.type != App1Codec::EnterBootloader);
            QVERIFY(request.type != App1Codec::BlBegin);
        }
    }

    void appliesSemanticVersionPolicy()
    {
        const DeviceInfo device = [] {
            DeviceInfo value;
            value.productId = 0x00010001U;
            value.hardwareRevision = 1U;
            return value;
        }();
        const FirmwareInfo installed = info(2U, 0U, 0U);
        QString description;
        bool downgrade = false;
        QVERIFY(FirmwareUpgradeController::checkCompatibility(
            info(3U, 0U, 0U), device, &installed, false,
            &description, &downgrade));
        QVERIFY(FirmwareUpgradeController::checkCompatibility(
            info(2U, 0U, 0U, 0x00010001U, "fedcba987654"),
            device, &installed, false, &description, &downgrade));
        QVERIFY(!FirmwareUpgradeController::checkCompatibility(
            info(1U, 9U, 9U), device, &installed, false,
            &description, &downgrade));
        QVERIFY(downgrade);
        QVERIFY(FirmwareUpgradeController::checkCompatibility(
            info(1U, 9U, 9U), device, &installed, true,
            &description, &downgrade));
    }

    void encodesBeginV3AndNeverCrossesFourKiBBoundary()
    {
        FakeTransport transport;
        FirmwareUpgradeController controller(&transport);
        FirmwareImage candidate = image(info(2U, 0U, 0U));
        candidate.image.resize(5000);
        candidate.crc32 = App1Codec::crc32(candidate.image);
        controller.startUpgrade(upgradeDevice(UpgradeDevice::BootloaderMode),
                                candidate);
        answerInformationSequence(transport, App1Codec::BootloaderMode,
                                  header(1U, 0U, 0U));

        QByteArray hello;
        append16(hello, 2U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        respond(transport, App1Codec::BlHello, hello);

        respond(transport, App1Codec::BlStatus, statusPayload());
        const App1Frame begin = lastRequest(transport);
        QCOMPARE(begin.type, quint16(App1Codec::BlBegin));
        QCOMPARE(begin.payload.size(), 288);
        QCOMPARE(qFromLittleEndian<quint16>(
                     reinterpret_cast<const uchar *>(begin.payload.constData())),
                 quint16(3U));
        QCOMPARE(qFromLittleEndian<quint16>(
                     reinterpret_cast<const uchar *>(begin.payload.constData() + 2)),
                 quint16(FirmwareInfo::HeaderSize));
        QCOMPARE(qFromLittleEndian<quint32>(
                     reinterpret_cast<const uchar *>(begin.payload.constData() + 4)),
                 IntelHexParser::ApplicationBase);
        QCOMPARE(begin.payload.mid(16, 16).size(), 16);
        QVERIFY(begin.payload.mid(16, 16) != QByteArray(16, char(0)));
        QCOMPARE(begin.payload.mid(32, FirmwareInfo::HeaderSize),
                 candidate.firmwareInfo.raw);

        QByteArray offset;
        append32(offset, 4080U);
        respond(transport, App1Codec::BlBegin, offset);
        const App1Frame data = lastRequest(transport);
        QCOMPARE(data.type, quint16(App1Codec::BlData));
        QCOMPARE(qFromLittleEndian<quint32>(
                     reinterpret_cast<const uchar *>(data.payload.constData())),
                 quint32(4080U));
        QCOMPARE(qFromLittleEndian<quint16>(
                     reinterpret_cast<const uchar *>(data.payload.constData() + 4)),
                 quint16(16U));
    }

    void confirmsInstalledBuildAfterReboot()
    {
        FakeTransport transport;
        const UpgradeDevice boot = upgradeDevice(UpgradeDevice::BootloaderMode);
        transport.devices = {boot};
        FirmwareUpgradeController controller(&transport);
        const FirmwareImage candidate = image(info(2U, 1U, 0U));
        controller.startUpgrade(boot, candidate);
        answerInformationSequence(transport, App1Codec::BootloaderMode,
                                  header(1U, 0U, 0U));

        QByteArray hello;
        append16(hello, 2U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        respond(transport, App1Codec::BlHello, hello);
        respond(transport, App1Codec::BlStatus, statusPayload());

        QByteArray offset;
        append32(offset, 256U);
        respond(transport, App1Codec::BlBegin, offset);
        offset.clear();
        append32(offset, 496U);
        respond(transport, App1Codec::BlData, offset);
        offset.clear();
        append32(offset, 600U);
        respond(transport, App1Codec::BlData, offset);
        const App1Frame end = lastRequest(transport);
        QCOMPARE(end.type, quint16(App1Codec::BlEnd));
        QVERIFY(end.payload.isEmpty());

        respond(transport, App1Codec::BlEnd, {});
        QCOMPARE(lastRequest(transport).type, quint16(App1Codec::BlInstall));
        respond(transport, App1Codec::BlInstall, {});
        QCOMPARE(controller.stage(), FirmwareUpgradeController::MonitorRecovery);
        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::BlStatus));
        respond(transport, App1Codec::BlStatus, statusPayload(1U));
        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
        respond(transport, App1Codec::GetMode,
                modePayload(App1Codec::BootloaderMode));
        const int writesAfterBootloaderProbe = transport.writes.size();
        QTest::qWait(100);
        QCOMPARE(transport.writes.size(), writesAfterBootloaderProbe);

        UpgradeDevice app = boot;
        app.mode = UpgradeDevice::ApplicationMode;
        transport.devices = {app};
        QTRY_VERIFY(transport.writes.size() > writesAfterBootloaderProbe);
        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
        answerInformationSequence(transport, App1Codec::ApplicationMode,
                                  candidate.firmwareInfo.raw);
        QTRY_COMPARE(controller.stage(), FirmwareUpgradeController::Completed);
    }

    void acceptsBootloaderResetDuringRecovery()
    {
        FakeTransport transport;
        const UpgradeDevice boot = upgradeDevice(UpgradeDevice::BootloaderMode);
        transport.devices = {boot};
        FirmwareUpgradeController controller(&transport);
        const FirmwareImage candidate = image(info(2U, 1U, 0U));
        controller.startUpgrade(boot, candidate);
        answerInformationSequence(transport, App1Codec::BootloaderMode,
                                  header(1U, 0U, 0U));

        QByteArray hello;
        append16(hello, 2U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        respond(transport, App1Codec::BlHello, hello);
        respond(transport, App1Codec::BlStatus, statusPayload());

        QByteArray offset;
        append32(offset, 256U);
        respond(transport, App1Codec::BlBegin, offset);
        offset.clear();
        append32(offset, 496U);
        respond(transport, App1Codec::BlData, offset);
        offset.clear();
        append32(offset, 600U);
        respond(transport, App1Codec::BlData, offset);
        respond(transport, App1Codec::BlEnd, {});
        respond(transport, App1Codec::BlInstall, {});
        QCOMPARE(controller.stage(), FirmwareUpgradeController::MonitorRecovery);

        UpgradeDevice app = boot;
        app.mode = UpgradeDevice::ApplicationMode;
        transport.devices = {app};
        transport.disconnectNow();
        QCOMPARE(controller.stage(), FirmwareUpgradeController::WaitApplication);
        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::GetMode));
        answerInformationSequence(transport, App1Codec::ApplicationMode,
                                  candidate.firmwareInfo.raw);
        QTRY_COMPARE(controller.stage(), FirmwareUpgradeController::Completed);
    }

    void waitsForRecoveryStatusReplyBeforePollingAgain()
    {
        FakeTransport transport;
        const UpgradeDevice boot = upgradeDevice(UpgradeDevice::BootloaderMode);
        transport.devices = {boot};
        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(boot, image(info(2U, 1U, 0U)));
        answerInformationSequence(transport, App1Codec::BootloaderMode,
                                  header(1U, 0U, 0U));

        QByteArray hello;
        append16(hello, 2U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        respond(transport, App1Codec::BlHello, hello);
        respond(transport, App1Codec::BlStatus, statusPayload());

        QByteArray offset;
        append32(offset, 256U);
        respond(transport, App1Codec::BlBegin, offset);
        offset.clear();
        append32(offset, 496U);
        respond(transport, App1Codec::BlData, offset);
        offset.clear();
        append32(offset, 600U);
        respond(transport, App1Codec::BlData, offset);
        respond(transport, App1Codec::BlEnd, {});
        respond(transport, App1Codec::BlInstall, {});

        QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::BlStatus));
        const int requestCount = transport.writes.size();
        QTest::qWait(600);
        QCOMPARE(transport.writes.size(), requestCount);
        QCOMPARE(controller.stage(), FirmwareUpgradeController::MonitorRecovery);
    }

    void keepsRecoveryPhasesOutOfStaging_data()
    {
        QTest::addColumn<quint8>("phase");
        QTest::addColumn<bool>("afterInstall");
        QTest::newRow("staged-after-install") << quint8(3U) << true;
        QTest::newRow("backing-up-after-install") << quint8(4U) << true;
        QTest::newRow("backup-ready-after-install") << quint8(5U) << true;
        QTest::newRow("backing-up-after-reconnect") << quint8(4U) << false;
        QTest::newRow("backup-ready-after-reconnect") << quint8(5U) << false;
    }

    void keepsRecoveryPhasesOutOfStaging()
    {
        QFETCH(quint8, phase);
        QFETCH(bool, afterInstall);
        FakeTransport transport;
        const UpgradeDevice boot = upgradeDevice(UpgradeDevice::BootloaderMode);
        transport.devices = {boot};
        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(boot, image(info(2U, 1U, 0U)));
        answerInformationSequence(transport, App1Codec::BootloaderMode,
                                  header(1U, 0U, 0U));

        QByteArray hello;
        append16(hello, 2U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        respond(transport, App1Codec::BlHello, hello);
        if (afterInstall) {
            respond(transport, App1Codec::BlStatus, statusPayload());
            QByteArray offset;
            append32(offset, 256U);
            respond(transport, App1Codec::BlBegin, offset);
            offset.clear();
            append32(offset, 496U);
            respond(transport, App1Codec::BlData, offset);
            offset.clear();
            append32(offset, 600U);
            respond(transport, App1Codec::BlData, offset);
            respond(transport, App1Codec::BlEnd, {});
            respond(transport, App1Codec::BlInstall, {});
            QTRY_COMPARE(lastRequest(transport).type, quint16(App1Codec::BlStatus));
        }

        const int requestCount = transport.writes.size();
        respond(transport, App1Codec::BlStatus, statusPayload(phase));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::MonitorRecovery);
        QCOMPARE(transport.writes.size(), requestCount);
    }
};

QTEST_MAIN(FirmwareCoreTest)
#include "tst_firmwarecore.moc"

