#include "FirmwareInfo.h"

#include "App1Codec.h"

#include <QTextCodec>
#include <QtEndian>

namespace {
quint16 read16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 read32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint64 read64(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool decodeText(const QByteArray &bytes, int offset, int size,
                const QString &field, QString *value, QString *error)
{
    const QByteArray storage = bytes.mid(offset, size);
    const int terminator = storage.indexOf('\0');
    if (terminator < 0)
        return fail(error, field + QStringLiteral(" is not NUL terminated"));
    for (int index = terminator + 1; index < storage.size(); ++index) {
        if (storage.at(index) != '\0')
            return fail(error, field + QStringLiteral(" padding is not zero"));
    }
    QTextCodec::ConverterState state;
    const QString decoded = QTextCodec::codecForName("UTF-8")->toUnicode(
        storage.constData(), terminator, &state);
    if (state.invalidChars != 0)
        return fail(error, field + QStringLiteral(" contains invalid UTF-8"));
    if (decoded.isEmpty())
        return fail(error, field + QStringLiteral(" is empty"));
    *value = decoded;
    return true;
}
}

bool FirmwareInfo::decode(const QByteArray &bytes, FirmwareInfo *info,
                          QString *error)
{
    if (!info)
        return fail(error, QStringLiteral("firmware information output is null"));
    if (bytes.size() != HeaderSize)
        return fail(error, QStringLiteral("firmware information length is invalid"));
    if (bytes.left(4) != QByteArrayLiteral("FWI1"))
        return fail(error, QStringLiteral("firmware information magic is invalid"));
    if (read16(bytes, 0x04) != 1U || read16(bytes, 0x06) != HeaderSize)
        return fail(error, QStringLiteral("firmware information format is invalid"));
    if (App1Codec::crc32(bytes.left(0xFC)) != read32(bytes, 0xFC))
        return fail(error, QStringLiteral("firmware information CRC mismatch"));

    const quint16 parsedFlags = read16(bytes, 0x16);
    const quint16 buildFlags = parsedFlags
        & quint16(DebugBuildFlag | ReleaseBuildFlag);
    if ((parsedFlags & quint16(~0x0007U)) != 0U
        || (buildFlags != DebugBuildFlag && buildFlags != ReleaseBuildFlag)
        || ((parsedFlags & DirtyFlag) != 0U
            && buildFlags == ReleaseBuildFlag)) {
        return fail(error, QStringLiteral("firmware information flags are invalid"));
    }
    if (read16(bytes, 0x0E) < read16(bytes, 0x0C))
        return fail(error, QStringLiteral("hardware revision range is invalid"));
    if (read32(bytes, 0x1C) != ExpectedImageBase
        || read32(bytes, 0x20) != ExpectedVectorBase
        || read32(bytes, 0x24) != ExpectedRegionSize) {
        return fail(error, QStringLiteral("firmware fixed address fields are invalid"));
    }
    for (int index = 0xA0; index < 0xFC; ++index) {
        if (bytes.at(index) != '\0')
            return fail(error, QStringLiteral("firmware reserved bytes are nonzero"));
    }

    FirmwareInfo parsed;
    parsed.raw = bytes;
    parsed.productId = read32(bytes, 0x08);
    parsed.hardwareRevisionMinimum = read16(bytes, 0x0C);
    parsed.hardwareRevisionMaximum = read16(bytes, 0x0E);
    parsed.versionMajor = read16(bytes, 0x10);
    parsed.versionMinor = read16(bytes, 0x12);
    parsed.versionPatch = read16(bytes, 0x14);
    parsed.flags = parsedFlags;
    parsed.buildNumber = read32(bytes, 0x18);
    parsed.imageBase = read32(bytes, 0x1C);
    parsed.vectorBase = read32(bytes, 0x20);
    parsed.applicationRegionSize = read32(bytes, 0x24);
    parsed.buildTimeUtc = read64(bytes, 0x28);
    if (!decodeText(bytes, 0x30, 32, QStringLiteral("vendor_name"),
                    &parsed.vendorName, error)
        || !decodeText(bytes, 0x50, 32, QStringLiteral("device_name"),
                       &parsed.deviceName, error)
        || !decodeText(bytes, 0x70, 32, QStringLiteral("firmware_name"),
                       &parsed.firmwareName, error)
        || !decodeText(bytes, 0x90, 16, QStringLiteral("git_commit"),
                       &parsed.gitCommit, error)) {
        return false;
    }
    *info = parsed;
    if (error)
        error->clear();
    return true;
}

int FirmwareInfo::compareSemanticVersion(const FirmwareInfo &left,
                                         const FirmwareInfo &right)
{
    if (left.versionMajor != right.versionMajor)
        return left.versionMajor < right.versionMajor ? -1 : 1;
    if (left.versionMinor != right.versionMinor)
        return left.versionMinor < right.versionMinor ? -1 : 1;
    if (left.versionPatch != right.versionPatch)
        return left.versionPatch < right.versionPatch ? -1 : 1;
    return 0;
}

QString FirmwareInfo::versionString() const
{
    return QStringLiteral("%1.%2.%3")
        .arg(versionMajor).arg(versionMinor).arg(versionPatch);
}

QString FirmwareInfo::buildTypeString() const
{
    return isDebugBuild() ? QStringLiteral("Debug") : QStringLiteral("Release");
}

bool FirmwareInfo::sameBuildIdentity(const FirmwareInfo &other) const
{
    return versionMajor == other.versionMajor
        && versionMinor == other.versionMinor
        && versionPatch == other.versionPatch
        && buildNumber == other.buildNumber
        && gitCommit == other.gitCommit;
}
