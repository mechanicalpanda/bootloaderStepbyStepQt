#include <QtTest>

#include "App1Codec.h"
#include "DeviceInfo.h"
#include "FirmwareInfo.h"
#include "IntelHexParser.h"

#include <QtEndian>

class FirmwareInfoTest : public QObject
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

    static void setText(QByteArray &data, int offset, int size,
                        const QByteArray &text)
    {
        QVERIFY(text.size() < size);
        data.replace(offset, text.size(), text);
    }

    static QByteArray validHeader()
    {
        QByteArray header(FirmwareInfo::HeaderSize, char(0));
        header.replace(0, 4, QByteArrayLiteral("FWI1"));
        write16(header, 0x04, 1U);
        write16(header, 0x06, FirmwareInfo::HeaderSize);
        write32(header, 0x08, 0x00010001U);
        write16(header, 0x0C, 1U);
        write16(header, 0x0E, 2U);
        write16(header, 0x10, 1U);
        write16(header, 0x12, 2U);
        write16(header, 0x14, 3U);
        write16(header, 0x16, FirmwareInfo::DebugBuildFlag);
        write32(header, 0x18, 42U);
        write32(header, 0x1C, IntelHexParser::ApplicationBase);
        write32(header, 0x20, IntelHexParser::VectorBase);
        write32(header, 0x24, IntelHexParser::ApplicationSize);
        write64(header, 0x28, 1700000000ULL);
        setText(header, 0x30, 32, QByteArrayLiteral("MYFOC"));
        setText(header, 0x50, 32,
                QByteArrayLiteral("MYFOC Motor Controller"));
        setText(header, 0x70, 32,
                QByteArrayLiteral("FOCTEST Application"));
        setText(header, 0x90, 16, QByteArrayLiteral("0123456789ab"));
        write32(header, 0xFC, App1Codec::crc32(header.left(0xFC)));
        return header;
    }

    static QByteArray record(quint16 address, quint8 type,
                             const QByteArray &data)
    {
        QByteArray raw;
        raw.append(char(data.size()));
        raw.append(char(address >> 8));
        raw.append(char(address));
        raw.append(char(type));
        raw.append(data);
        quint8 sum = 0U;
        for (char value : raw)
            sum = quint8(sum + quint8(value));
        raw.append(char(quint8(0U - sum)));
        return ":" + raw.toHex().toUpper() + "\n";
    }

    static QByteArray validHex()
    {
        QByteArray image(0x420, char(0xFF));
        image.replace(0, FirmwareInfo::HeaderSize, validHeader());
        write32(image, 0x200, 0x20020000U);
        write32(image, 0x204, 0x08020401U);
        image[0x400] = char(0x00);

        QByteArray hex = record(0, 4, QByteArray::fromHex("0802"));
        for (int offset = 0; offset < image.size(); offset += 16)
            hex += record(quint16(offset), 0, image.mid(offset, 16));
        hex += record(0, 1, {});
        return hex;
    }

private slots:
    void decodesGoldenFirmwareHeader()
    {
        FirmwareInfo info;
        QString error;
        const QByteArray header = validHeader();
        QVERIFY2(FirmwareInfo::decode(header, &info, &error),
                 qPrintable(error));
        QCOMPARE(info.raw, header);
        QCOMPARE(info.productId, quint32(0x00010001U));
        QCOMPARE(info.hardwareRevisionMinimum, quint16(1U));
        QCOMPARE(info.hardwareRevisionMaximum, quint16(2U));
        QCOMPARE(info.versionString(), QStringLiteral("1.2.3"));
        QCOMPARE(info.vendorName, QStringLiteral("MYFOC"));
        QCOMPARE(info.gitCommit, QStringLiteral("0123456789ab"));
        QVERIFY(info.isDebugBuild());
        QVERIFY(!info.isDirty());
    }

    void rejectsMalformedFirmwareHeaders_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<QString>("message");

        QByteArray badCrc = validHeader();
        badCrc[8] = char(badCrc.at(8) ^ 1);
        QTest::newRow("crc") << badCrc << QStringLiteral("CRC");

        QByteArray badFlags = validHeader();
        write16(badFlags, 0x16, FirmwareInfo::DebugBuildFlag
                              | FirmwareInfo::ReleaseBuildFlag);
        write32(badFlags, 0xFC, App1Codec::crc32(badFlags.left(0xFC)));
        QTest::newRow("flags") << badFlags << QStringLiteral("flags");

        QByteArray badUtf8 = validHeader();
        badUtf8[0x30] = char(0xC3);
        badUtf8[0x31] = char(0x28);
        write32(badUtf8, 0xFC, App1Codec::crc32(badUtf8.left(0xFC)));
        QTest::newRow("utf8") << badUtf8 << QStringLiteral("UTF-8");

        QByteArray missingNul = validHeader();
        missingNul.replace(0x30, 32, QByteArray(32, 'A'));
        write32(missingNul, 0xFC,
                App1Codec::crc32(missingNul.left(0xFC)));
        QTest::newRow("nul") << missingNul << QStringLiteral("terminated");

        QByteArray reserved = validHeader();
        reserved[0xA0] = char(1);
        write32(reserved, 0xFC,
                App1Codec::crc32(reserved.left(0xFC)));
        QTest::newRow("reserved") << reserved << QStringLiteral("reserved");

        QByteArray address = validHeader();
        write32(address, 0x20, 0x08020000U);
        write32(address, 0xFC, App1Codec::crc32(address.left(0xFC)));
        QTest::newRow("address") << address << QStringLiteral("address");
    }

    void rejectsMalformedFirmwareHeaders()
    {
        QFETCH(QByteArray, header);
        QFETCH(QString, message);
        FirmwareInfo info;
        QString error;
        QVERIFY(!FirmwareInfo::decode(header, &info, &error));
        QVERIFY2(error.contains(message, Qt::CaseInsensitive),
                 qPrintable(error));
    }

    void decodesDeviceInformation()
    {
        QByteArray payload(DeviceInfo::PayloadSize, char(0));
        write16(payload, 0, 1U);
        payload[2] = char(App1Codec::ApplicationMode);
        write32(payload, 4, 0x00010001U);
        write16(payload, 8, 1U);
        write32(payload, 12, App1Codec::CanEnterBootloader
                            | App1Codec::CanQueryDeviceInfo
                            | App1Codec::CanQueryFirmwareInfo);
        payload.replace(16, 12, QByteArray::fromHex(
            "001E00453432511933373439"));
        DeviceInfo info;
        QString error;
        QVERIFY2(DeviceInfo::decode(payload, &info, &error),
                 qPrintable(error));
        QCOMPARE(info.productId, quint32(0x00010001U));
        QCOMPARE(info.hardwareRevision, quint16(1U));
        QCOMPARE(info.uniqueId.toHex().toUpper(),
                 QByteArrayLiteral("001E00453432511933373439"));
    }

    void parsesNewLayoutAndRejectsOldVectorLayout()
    {
        FirmwareImage image;
        QString error;
        QVERIFY2(IntelHexParser::parse(validHex(), &image, &error),
                 qPrintable(error));
        QCOMPARE(image.firmwareInfo.versionString(),
                 QStringLiteral("1.2.3"));
        QCOMPARE(image.image.mid(0, FirmwareInfo::HeaderSize),
                 image.firmwareInfo.raw);

        QByteArray old = validHex();
        QByteArray imageBytes(0x420, char(0xFF));
        write32(imageBytes, 0, 0x20020000U);
        write32(imageBytes, 4, 0x08020401U);
        old = record(0, 4, QByteArray::fromHex("0802"));
        for (int offset = 0; offset < imageBytes.size(); offset += 16)
            old += record(quint16(offset), 0, imageBytes.mid(offset, 16));
        old += record(0, 1, {});
        QVERIFY(!IntelHexParser::parse(old, &image, &error));
        QVERIFY(error.contains(QStringLiteral("firmware"),
                               Qt::CaseInsensitive));
    }

    void supportsFullFirmwareInformationPayload()
    {
        const QByteArray payload(320, char(0x5A));
        const QByteArray frame = App1Codec::encodeRequest(
            App1Codec::GetFirmwareInfo, 7U, payload);
        QVERIFY(!frame.isEmpty());
        QCOMPARE(frame.size(), App1Codec::HeaderSize + payload.size());
    }
};

QTEST_APPLESS_MAIN(FirmwareInfoTest)
#include "tst_firmwareinfo.moc"

