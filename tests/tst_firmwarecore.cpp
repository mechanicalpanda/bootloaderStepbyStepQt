#include <QtTest>

#include "App1Codec.h"
#include "FirmwareUpgradeController.h"
#include "FirmwareUpgradeDialog.h"
#include "IntelHexParser.h"
#include "UpgradeTransport.h"
#include "WinUsbDeviceDiscovery.h"

class FakeTransport : public UpgradeTransport
{
public:
    QList<UpgradeDevice> devices;
    QList<QByteArray> writes;
    bool opened = false;

    QList<UpgradeDevice> discover(QString *error) override
    {
        if (error) error->clear();
        return devices;
    }
    bool open(const UpgradeDevice &, QString *error) override
    {
        opened = true;
        if (error) error->clear();
        return true;
    }
    void close() override { opened = false; }
    bool write(const QByteArray &data, QString *error) override
    {
        writes.append(data);
        if (error) error->clear();
        return true;
    }
    void deliver(const QByteArray &data) { emit dataReceived(data); }
    void disconnectNow() { opened = false; emit disconnected(); }
};

class FirmwareCoreTest : public QObject
{
    Q_OBJECT

private:
    static void append16(QByteArray &data, quint16 value)
    {
        data.append(char(value));
        data.append(char(value >> 8));
    }

    static void append32(QByteArray &data, quint32 value)
    {
        data.append(char(value));
        data.append(char(value >> 8));
        data.append(char(value >> 16));
        data.append(char(value >> 24));
    }

    static QByteArray record(quint16 address, quint8 type, const QByteArray &data)
    {
        QByteArray raw;
        raw.append(char(data.size()));
        raw.append(char(address >> 8));
        raw.append(char(address));
        raw.append(char(type));
        raw.append(data);
        quint8 sum = 0;
        for (char byte : raw)
            sum = quint8(sum + quint8(byte));
        raw.append(char(quint8(0U - sum)));
        return ":" + raw.toHex().toUpper() + "\n";
    }

    static QByteArray modePayload(quint8 mode, quint32 capabilities)
    {
        QByteArray payload;
        append16(payload, App1Codec::ModeProtocolVersion);
        payload.append(char(mode));
        payload.append(char(0));
        append32(payload, capabilities);
        return payload;
    }

private slots:
    void exposesRequiredUpgradeControls()
    {
        FirmwareUpgradeDialog dialog;
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("deviceCombo")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("refreshButton")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("firmwarePath")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("browseButton")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("upgradeProgress")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("startButton")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("cancelButton")));
        QVERIFY(dialog.findChild<QObject *>(QStringLiteral("eventLog")));
    }

    void parsesWinUsbDeviceIdentityFromInterfacePath()
    {
        UpgradeDevice device;
        const QString path = QStringLiteral(
            R"(\\?\usb#vid_cafe&pid_4070&mi_02#ABC123#{6e15414d-b3e8-4b08-9b73-73db7e6a0f40})");
        QVERIFY(WinUsbDeviceDiscovery::parseDevicePath(path, &device));
        QCOMPARE(device.vid, quint16(0xCAFE));
        QCOMPARE(device.pid, quint16(0x4070));
        QCOMPARE(device.serial, QStringLiteral("ABC123"));
        QCOMPARE(device.mode, UpgradeDevice::UnknownMode);
        QVERIFY(device.displayName().contains(QStringLiteral("Unknown")));

        QVERIFY(!WinUsbDeviceDiscovery::parseDevicePath(
            QStringLiteral(
                R"(\\?\usb#vid_cafe&pid_4071&mi_00#OLD#{guid})"),
            &device));

        QVERIFY(!WinUsbDeviceDiscovery::parseDevicePath(
            QStringLiteral(R"(\\?\usb#vid_1234&pid_5678#OTHER#{guid})"),
            &device));
    }

    void parsesExtendedLinearAddressAndFillsGaps()
    {
        QByteArray hex;
        hex += record(0, 4, QByteArray::fromHex("0802"));
        hex += record(0, 0, QByteArray::fromHex("0000012011000208"));
        hex += record(0x10, 0, QByteArray::fromHex("AABB"));
        hex += record(0, 1, {});

        FirmwareImage image;
        QString error;
        QVERIFY2(IntelHexParser::parse(hex, &image, &error), qPrintable(error));
        QCOMPARE(image.baseAddress, quint32(0x08020000));
        QCOMPARE(image.image.size(), 18);
        QCOMPARE(image.image.mid(8, 8), QByteArray(8, char(0xFF)));
        QCOMPARE(image.image.right(2), QByteArray::fromHex("AABB"));
        QCOMPARE(image.crc32, App1Codec::crc32(image.image));
        QCOMPARE(image.imageVersion, image.crc32);
    }

    void rejectsBadChecksumAndOutOfRangeData()
    {
        FirmwareImage image;
        QString error;
        QByteArray bad = record(0, 4, QByteArray::fromHex("0802"));
        bad[bad.size() - 3] = (bad[bad.size() - 3] == '0') ? '1' : '0';
        QVERIFY(!IntelHexParser::parse(bad, &image, &error));
        QVERIFY(error.contains("checksum", Qt::CaseInsensitive));

        QByteArray outside;
        outside += record(0, 4, QByteArray::fromHex("0801"));
        outside += record(0, 0, QByteArray::fromHex("0102"));
        outside += record(0, 1, {});
        QVERIFY(!IntelHexParser::parse(outside, &image, &error));
        QVERIFY(error.contains("range", Qt::CaseInsensitive));

        QByteArray shortImage;
        shortImage += record(0, 4, QByteArray::fromHex("0802"));
        shortImage += record(0, 0, QByteArray::fromHex("0000012001010208"));
        shortImage += record(0, 1, {});
        QVERIFY(!IntelHexParser::parse(shortImage, &image, &error));
        QVERIFY(error.contains("vector", Qt::CaseInsensitive));

        QByteArray trailing;
        trailing += record(0, 4, QByteArray::fromHex("0802"));
        trailing += record(0, 0, QByteArray::fromHex("0000012005000208"));
        trailing += record(0, 1, {});
        trailing += record(8, 0, QByteArray::fromHex("AABBCCDD"));
        QVERIFY(!IntelHexParser::parse(trailing, &image, &error));
        QVERIFY(error.contains("EOF", Qt::CaseInsensitive));
    }

    void encodesAndDecodesApp1Frames()
    {
        const QByteArray payload = QByteArray::fromHex("01020304");
        QByteArray encoded = App1Codec::encodeRequest(
            App1Codec::BlBegin, 0x12345678U, payload);
        QCOMPARE(encoded.size(), App1Codec::HeaderSize + payload.size());

        App1Frame frame;
        QString error;
        QVERIFY2(App1Codec::decode(encoded, &frame, &error), qPrintable(error));
        QCOMPARE(frame.type, quint16(App1Codec::BlBegin));
        QCOMPARE(frame.sequence, quint32(0x12345678U));
        QCOMPARE(frame.flags, quint16(0));
        QCOMPARE(frame.payload, payload);

        encoded[20] = char(encoded.at(20) ^ 1);
        QVERIFY(!App1Codec::decode(encoded, &frame, &error));
        QVERIFY(error.contains("header CRC", Qt::CaseInsensitive));
    }

    void extractsCrossPacketFrameFromStream()
    {
        QByteArray stream = QByteArray("noise");
        const QByteArray expected = App1Codec::encodeResponse(
            App1Codec::BlStatus, 4U, false, QByteArray::fromHex("0102"));
        stream += expected.left(7);
        App1Frame frame;
        QString error;
        QVERIFY(!App1Codec::takeFrame(&stream, &frame, &error));
        stream += expected.mid(7);
        QVERIFY2(App1Codec::takeFrame(&stream, &frame, &error), qPrintable(error));
        QCOMPARE(frame.type, quint16(App1Codec::BlStatus));
        QCOMPARE(frame.payload, QByteArray::fromHex("0102"));
        QVERIFY(stream.isEmpty());
    }

    void refreshQueriesModeInsteadOfTrustingPid()
    {
        FakeTransport transport;
        UpgradeDevice device;
        device.path = QStringLiteral("fake-device");
        device.serial = QStringLiteral("ABC123");
        device.product = QStringLiteral("MYFOC Motor Controller");
        device.vid = 0xCAFE;
        device.pid = 0x4070;
        transport.devices = {device};

        FirmwareUpgradeController controller(&transport);
        QSignalSpy devicesSpy(&controller,
                              &FirmwareUpgradeController::devicesChanged);
        controller.refreshDevices();
        QCOMPARE(transport.writes.size(), 1);
        QCOMPARE(devicesSpy.count(), 0);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::ApplicationMode,
                        App1Codec::CanEnterBootloader)));
        QCOMPARE(devicesSpy.count(), 1);
        const QList<UpgradeDevice> resolved =
            qvariant_cast<QList<UpgradeDevice>>(devicesSpy.last().at(0));
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().mode, UpgradeDevice::ApplicationMode);
    }

    void advancesProgressOnlyFromBootloaderAcknowledgements()
    {
        FakeTransport transport;
        UpgradeDevice boot;
        boot.path = QStringLiteral("fake-boot");
        boot.serial = QStringLiteral("ABC123");
        boot.product = QStringLiteral("MYFOC Motor Controller");
        boot.vid = 0xCAFE;
        boot.pid = 0x4070;
        boot.mode = UpgradeDevice::BootloaderMode;
        transport.devices = {boot};

        FirmwareImage image;
        image.baseAddress = IntelHexParser::ApplicationBase;
        image.image = QByteArray(300, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.setRequestTimeout(50);
        QSignalSpy progressSpy(&controller,
            &FirmwareUpgradeController::progressChanged);
        controller.startUpgrade(boot, image);
        QCOMPARE(controller.stage(), FirmwareUpgradeController::QueryMode);
        QCOMPARE(transport.writes.size(), 1);

        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::BootloaderMode,
                        App1Codec::CanUpgrade | App1Codec::CanReboot)));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Hello);
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlHello));
        QByteArray hello;
        append16(hello, 1U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, hello));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Hello);

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlStatus));
        QByteArray status;
        append32(status, 0U);
        append32(status, 0U);
        append32(status, 0U);
        append16(status, 0U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, status));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Begin);
        const int beginWriteCount = transport.writes.size();
        QTest::qWait(200);
        QCOMPARE(transport.writes.size(), beginWriteCount);

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlBegin));
        QByteArray offset;
        append32(offset, 0U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, offset));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Transfer);
        QCOMPARE(progressSpy.count(), 0);

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlData));
        QCOMPARE(request.payload.size(), 246);
        offset.clear();
        append32(offset, 240U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, offset));
        QCOMPARE(progressSpy.count(), 1);
        QCOMPARE(progressSpy.last().at(0).toUInt(), quint32(240U));
        QCOMPARE(progressSpy.last().at(1).toUInt(), quint32(300U));

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlData));
        offset.clear();
        append32(offset, 300U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, offset));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::End);
        QCOMPARE(progressSpy.last().at(0).toUInt(), quint32(300U));

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlEnd));
        UpgradeDevice app = boot;
        app.path = QStringLiteral("fake-app");
        app.mode = UpgradeDevice::ApplicationMode;
        app.product = QStringLiteral("MYFOC Motor Controller");
        transport.devices = {app};
        const int endWriteCount = transport.writes.size();
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, {}));
        QTRY_VERIFY(transport.writes.size() > endWriteCount);
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::ApplicationMode,
                        App1Codec::CanEnterBootloader)));
        QTRY_COMPARE(controller.stage(), FirmwareUpgradeController::Completed);
    }

    void rejectsMalformedApplicationRebootAcknowledgement()
    {
        FakeTransport transport;
        UpgradeDevice app;
        app.path = QStringLiteral("fake-app");
        app.serial = QStringLiteral("ABC123");
        app.vid = 0xCAFE;
        app.pid = 0x4070;
        app.mode = UpgradeDevice::ApplicationMode;
        transport.devices = {app};
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(app, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::ApplicationMode,
                        App1Codec::CanEnterBootloader)));
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::EnterBootloader));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, {}));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Failed);
    }

    void followsApplicationDisconnectIntoBootloader()
    {
        FakeTransport transport;
        UpgradeDevice app;
        app.path = QStringLiteral("fake-app");
        app.serial = QStringLiteral("ABC123");
        app.vid = 0xCAFE;
        app.pid = 0x4070;
        app.mode = UpgradeDevice::ApplicationMode;
        UpgradeDevice boot = app;
        boot.path = QStringLiteral("fake-boot");
        boot.mode = UpgradeDevice::BootloaderMode;
        transport.devices = {app};
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(app, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::ApplicationMode,
                        App1Codec::CanEnterBootloader)));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::EnterBootloader);
        transport.devices = {boot};
        const int enterWriteCount = transport.writes.size();
        transport.disconnectNow();
        QTRY_VERIFY(transport.writes.size() > enterWriteCount);
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::GetMode));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::BootloaderMode,
                        App1Codec::CanUpgrade | App1Codec::CanReboot)));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Hello);
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlHello));
    }

    void rejectsMalformedModeResponse_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("short")
            << QByteArray(7, char(0));
        QByteArray badVersion = modePayload(
            App1Codec::ApplicationMode,
            App1Codec::CanEnterBootloader);
        badVersion[0] = char(2);
        QTest::newRow("version") << badVersion;
        QByteArray badReserved = modePayload(
            App1Codec::ApplicationMode,
            App1Codec::CanEnterBootloader);
        badReserved[3] = char(1);
        QTest::newRow("reserved") << badReserved;
        QTest::newRow("mode")
            << modePayload(3U, App1Codec::CanEnterBootloader);
        QTest::newRow("capabilities")
            << modePayload(App1Codec::ApplicationMode,
                           App1Codec::CanUpgrade);
    }

    void rejectsMalformedModeResponse()
    {
        QFETCH(QByteArray, payload);
        FakeTransport transport;
        UpgradeDevice device;
        device.path = QStringLiteral("fake-device");
        device.serial = QStringLiteral("ABC123");
        device.vid = 0xCAFE;
        device.pid = 0x4070;
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(device, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, payload));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Failed);
    }

    void ignoresWrongModeSequence()
    {
        FakeTransport transport;
        UpgradeDevice device;
        device.path = QStringLiteral("fake-device");
        device.serial = QStringLiteral("ABC123");
        device.vid = 0xCAFE;
        device.pid = 0x4070;
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(device, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        const QByteArray payload = modePayload(
            App1Codec::BootloaderMode,
            App1Codec::CanUpgrade | App1Codec::CanReboot);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence + 1U, false, payload));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::QueryMode);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, payload));
        QCOMPARE(controller.stage(), FirmwareUpgradeController::Hello);
    }

    void failsModeQueryAfterBoundedRetries()
    {
        FakeTransport transport;
        UpgradeDevice device;
        device.path = QStringLiteral("fake-device");
        device.serial = QStringLiteral("ABC123");
        device.vid = 0xCAFE;
        device.pid = 0x4070;
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.setRequestTimeout(50);
        controller.startUpgrade(device, image);
        QTRY_COMPARE(controller.stage(), FirmwareUpgradeController::Failed);
        QCOMPARE(transport.writes.size(), 4);
    }

    void ignoresDifferentSerialDuringModeTransition()
    {
        FakeTransport transport;
        UpgradeDevice app;
        app.path = QStringLiteral("fake-app");
        app.serial = QStringLiteral("ABC123");
        app.vid = 0xCAFE;
        app.pid = 0x4070;
        UpgradeDevice other = app;
        other.path = QStringLiteral("other-device");
        other.serial = QStringLiteral("XYZ789");
        transport.devices = {app};
        FirmwareImage image;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        controller.startUpgrade(app, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::ApplicationMode,
                        App1Codec::CanEnterBootloader)));
        const int writeCount = transport.writes.size();
        transport.devices = {other};
        transport.disconnectNow();
        QTest::qWait(600);
        QCOMPARE(controller.stage(), FirmwareUpgradeController::WaitBootloader);
        QCOMPARE(transport.writes.size(), writeCount);
    }

    void reportsStorageErrorDetail_data()
    {
        QTest::addColumn<int>("detail");
        QTest::addColumn<QString>("expectedText");
        QTest::newRow("legacy") << 0x00 << QStringLiteral("error 5");
        QTest::newRow("erase") << 0x10
            << QStringLiteral("storage detail 0x10: Sector 11 erase failed");
        QTest::newRow("program-word-3") << 0x33
            << QStringLiteral("storage detail 0x33: state word 3 program failed");
        QTest::newRow("verify-word-3") << 0x43
            << QStringLiteral("storage detail 0x43: state word 3 readback mismatch");
        QTest::newRow("unknown") << 0x99
            << QStringLiteral("storage detail 0x99: unknown storage failure");
    }

    void reportsStorageErrorDetail()
    {
        QFETCH(int, detail);
        QFETCH(QString, expectedText);
        FakeTransport transport;
        UpgradeDevice boot;
        boot.path = QStringLiteral("fake-boot");
        boot.serial = QStringLiteral("ABC123");
        boot.product = QStringLiteral("MYFOC Motor Controller");
        boot.vid = 0xCAFE;
        boot.pid = 0x4070;
        boot.mode = UpgradeDevice::BootloaderMode;
        transport.devices = {boot};
        FirmwareImage image;
        image.baseAddress = IntelHexParser::ApplicationBase;
        image.image = QByteArray(8, char(0x5A));
        image.crc32 = App1Codec::crc32(image.image);
        image.imageVersion = image.crc32;

        FirmwareUpgradeController controller(&transport);
        QSignalSpy finishedSpy(&controller,
                               &FirmwareUpgradeController::finished);
        controller.startUpgrade(boot, image);
        App1Frame request;
        QString error;
        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false,
            modePayload(App1Codec::BootloaderMode,
                        App1Codec::CanUpgrade | App1Codec::CanReboot)));

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QByteArray hello;
        append16(hello, 1U);
        append16(hello, 0U);
        append32(hello, IntelHexParser::ApplicationBase);
        append32(hello, IntelHexParser::ApplicationSize);
        append16(hello, 240U);
        append16(hello, 0U);
        append32(hello, 0U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, hello));

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QByteArray status;
        append32(status, 0U);
        append32(status, 0U);
        append32(status, 0U);
        append16(status, 0U);
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, false, status));

        QVERIFY(App1Codec::decode(transport.writes.last(), &request, &error));
        QCOMPARE(request.type, quint16(App1Codec::BlBegin));
        QByteArray errorPayload;
        errorPayload.append(char(5));
        errorPayload.append(char(detail));
        transport.deliver(App1Codec::encodeResponse(
            request.type, request.sequence, true, errorPayload));

        QCOMPARE(controller.stage(), FirmwareUpgradeController::Failed);
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(finishedSpy.last().at(1).toString().contains(expectedText));
    }
};

QTEST_MAIN(FirmwareCoreTest)
#include "tst_firmwarecore.moc"
