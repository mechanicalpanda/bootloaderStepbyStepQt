#include "IntelHexParser.h"

#include "App1Codec.h"

#include <QFile>
#include <QMap>
#include <QtEndian>

namespace {
bool fail(QString *error, int line, const QString &message)
{
    if (error)
        *error = line > 0
            ? QStringLiteral("line %1: %2").arg(line).arg(message)
            : message;
    return false;
}

bool validHex(const QByteArray &text)
{
    for (char value : text) {
        if (!((value >= '0' && value <= '9')
              || (value >= 'a' && value <= 'f')
              || (value >= 'A' && value <= 'F')))
            return false;
    }
    return true;
}

quint32 readVectorWord(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}
}

bool IntelHexParser::parseFile(const QString &path, FirmwareImage *image,
                               QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, 0, QStringLiteral("cannot open HEX file: %1")
                    .arg(file.errorString()));
    return parse(file.readAll(), image, error);
}

bool IntelHexParser::parse(const QByteArray &hexText, FirmwareImage *image,
                           QString *error)
{
    if (!image)
        return fail(error, 0, QStringLiteral("output image is null"));
    QMap<quint32, quint8> bytes;
    quint32 addressBase = 0U;
    bool eofSeen = false;
    const QList<QByteArray> lines = hexText.split('\n');

    for (int index = 0; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const int lineNumber = index + 1;
        if (line.isEmpty())
            continue;
        if (eofSeen)
            return fail(error, lineNumber,
                        QStringLiteral("non-empty record appears after EOF"));
        if (!line.startsWith(':'))
            return fail(error, lineNumber,
                        QStringLiteral("record must start with ':'"));
        const QByteArray encoded = line.mid(1);
        if ((encoded.size() & 1) != 0 || !validHex(encoded))
            return fail(error, lineNumber,
                        QStringLiteral("record contains invalid hex"));
        const QByteArray raw = QByteArray::fromHex(encoded);
        if (raw.size() < 5)
            return fail(error, lineNumber,
                        QStringLiteral("record is too short"));
        const int count = quint8(raw.at(0));
        if (raw.size() != count + 5)
            return fail(error, lineNumber,
                        QStringLiteral("record length mismatch"));
        quint8 sum = 0U;
        for (char value : raw)
            sum = quint8(sum + quint8(value));
        if (sum != 0U)
            return fail(error, lineNumber, QStringLiteral("checksum mismatch"));
        const quint16 address = (quint16(quint8(raw.at(1))) << 8U)
                              | quint8(raw.at(2));
        const quint8 type = quint8(raw.at(3));
        const QByteArray data = raw.mid(4, count);

        if (type == 0x00U) {
            const quint32 absolute = addressBase + address;
            if (absolute < ApplicationBase
                || quint64(absolute) + quint64(count)
                   > quint64(ApplicationBase) + ApplicationSize) {
                return fail(error, lineNumber,
                            QStringLiteral("data address out of application range"));
            }
            for (int byteIndex = 0; byteIndex < count; ++byteIndex) {
                const quint32 target = absolute + quint32(byteIndex);
                const quint8 value = quint8(data.at(byteIndex));
                if (bytes.contains(target) && bytes.value(target) != value)
                    return fail(error, lineNumber,
                                QStringLiteral("conflicting overlapping data"));
                bytes.insert(target, value);
            }
        } else if (type == 0x01U) {
            if (count != 0 || address != 0U)
                return fail(error, lineNumber,
                            QStringLiteral("invalid EOF record"));
            eofSeen = true;
        } else if (type == 0x02U) {
            if (count != 2 || address != 0U)
                return fail(error, lineNumber,
                            QStringLiteral("invalid extended segment record"));
            addressBase = quint32((quint16(quint8(data.at(0))) << 8U)
                                  | quint8(data.at(1))) << 4U;
        } else if (type == 0x04U) {
            if (count != 2 || address != 0U)
                return fail(error, lineNumber,
                            QStringLiteral("invalid extended linear record"));
            addressBase = quint32((quint16(quint8(data.at(0))) << 8U)
                                  | quint8(data.at(1))) << 16U;
        } else if (type == 0x03U || type == 0x05U) {
            if (count != 4 || address != 0U)
                return fail(error, lineNumber,
                            QStringLiteral("invalid start-address record"));
        } else {
            return fail(error, lineNumber,
                        QStringLiteral("unsupported record type"));
        }
    }

    if (!eofSeen)
        return fail(error, 0, QStringLiteral("HEX EOF record is missing"));
    if (bytes.isEmpty())
        return fail(error, 0, QStringLiteral("HEX contains no application data"));
    const quint32 highest = bytes.lastKey();
    QByteArray binary(int(highest - ApplicationBase + 1U), char(0xFF));
    for (auto it = bytes.cbegin(); it != bytes.cend(); ++it)
        binary[int(it.key() - ApplicationBase)] = char(it.value());

    for (quint32 offset = 0U;
         offset < quint32(FirmwareInfo::HeaderSize); ++offset) {
        if (!bytes.contains(ApplicationBase + offset))
            return fail(error, 0,
                        QStringLiteral("firmware information header is incomplete"));
    }
    FirmwareInfo firmwareInfo;
    QString firmwareError;
    if (!FirmwareInfo::decode(binary.left(FirmwareInfo::HeaderSize),
                              &firmwareInfo, &firmwareError))
        return fail(error, 0, firmwareError);

    const int vectorOffset = int(VectorBase - ApplicationBase);
    if (binary.size() < vectorOffset + 8)
        return fail(error, 0,
                    QStringLiteral("application vector table is incomplete"));
    for (int offset = 0; offset < 8; ++offset) {
        if (!bytes.contains(VectorBase + quint32(offset)))
            return fail(error, 0,
                        QStringLiteral("application vector table is incomplete"));
    }
    const quint32 msp = readVectorWord(binary, vectorOffset);
    const quint32 reset = readVectorWord(binary, vectorOffset + 4);
    const bool mspValid = ((msp > 0x20000000U && msp <= 0x20020000U)
                           || (msp > 0x10000000U && msp <= 0x10010000U))
                       && (msp & 7U) == 0U;
    const quint32 resetAddress = reset & ~quint32(1U);
    if (!mspValid || (reset & 1U) == 0U
        || resetAddress < CodeBase
        || resetAddress >= ApplicationBase + quint32(binary.size())) {
        return fail(error, 0,
                    QStringLiteral("application vector table is invalid"));
    }

    image->baseAddress = ApplicationBase;
    image->image = binary;
    image->crc32 = App1Codec::crc32(binary);
    image->firmwareInfo = firmwareInfo;
    if (error)
        error->clear();
    return true;
}
