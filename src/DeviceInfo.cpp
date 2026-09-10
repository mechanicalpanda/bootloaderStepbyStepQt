#include "DeviceInfo.h"

#include "App1Codec.h"

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
}

bool DeviceInfo::decode(const QByteArray &bytes, DeviceInfo *info,
                        QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!info)
        return fail(QStringLiteral("device information output is null"));
    if (bytes.size() != PayloadSize)
        return fail(QStringLiteral("device information length is invalid"));
    if (read16(bytes, 0) != 1U || bytes.at(3) != '\0'
        || read16(bytes, 10) != 0U)
        return fail(QStringLiteral("device information format is invalid"));
    const quint8 mode = quint8(bytes.at(2));
    if (mode != App1Codec::ApplicationMode
        && mode != App1Codec::BootloaderMode)
        return fail(QStringLiteral("device information mode is invalid"));
    DeviceInfo parsed;
    parsed.mode = mode;
    parsed.productId = read32(bytes, 4);
    parsed.hardwareRevision = read16(bytes, 8);
    parsed.capabilities = read32(bytes, 12);
    parsed.uniqueId = bytes.mid(16, 12);
    *info = parsed;
    if (error)
        error->clear();
    return true;
}
