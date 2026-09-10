#include "App1Codec.h"

#include <QtEndian>

namespace {
constexpr quint32 Magic = 0x31505041U;
constexpr quint16 Version = 1U;

quint16 read16(const char *p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

quint32 read32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

void write16(char *p, quint16 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(p));
}

void write32(char *p, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(p));
}
}

quint32 App1Codec::crc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFU;
    for (char value : data) {
        crc ^= quint8(value);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

QByteArray App1Codec::encodeRequest(quint16 type, quint32 sequence,
                                    const QByteArray &payload)
{
    return encode(type, sequence, 0U, payload);
}

QByteArray App1Codec::encodeResponse(quint16 type, quint32 sequence, bool error,
                                     const QByteArray &payload)
{
    return encode(type, sequence,
                  quint16(ResponseFlag | (error ? ErrorFlag : 0U)), payload);
}

QByteArray App1Codec::encode(quint16 type, quint32 sequence, quint16 flags,
                             const QByteArray &payload)
{
    if (payload.size() > MaxPayloadSize)
        return {};
    QByteArray bytes(HeaderSize + payload.size(), char(0));
    write32(bytes.data(), Magic);
    write16(bytes.data() + 4, Version);
    write16(bytes.data() + 6, type);
    write32(bytes.data() + 8, sequence);
    write16(bytes.data() + 12, quint16(payload.size()));
    write16(bytes.data() + 14, flags);
    write32(bytes.data() + 16, crc32(payload));
    write32(bytes.data() + 20, crc32(bytes.left(20)));
    if (!payload.isEmpty())
        memcpy(bytes.data() + HeaderSize, payload.constData(), size_t(payload.size()));
    return bytes;
}

bool App1Codec::decode(const QByteArray &bytes, App1Frame *frame, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!frame)
        return fail(QStringLiteral("output frame is null"));
    if (bytes.size() < HeaderSize)
        return fail(QStringLiteral("frame is shorter than APP1 header"));
    if (read32(bytes.constData()) != Magic)
        return fail(QStringLiteral("APP1 magic mismatch"));
    if (read16(bytes.constData() + 4) != Version)
        return fail(QStringLiteral("APP1 version mismatch"));
    const quint16 payloadLength = read16(bytes.constData() + 12);
    const quint16 flags = read16(bytes.constData() + 14);
    if (payloadLength > MaxPayloadSize || bytes.size() != HeaderSize + payloadLength)
        return fail(QStringLiteral("APP1 length mismatch"));
    if ((flags & quint16(~(ResponseFlag | ErrorFlag))) != 0U)
        return fail(QStringLiteral("APP1 flags invalid"));
    if (read32(bytes.constData() + 20) != crc32(bytes.left(20)))
        return fail(QStringLiteral("APP1 header CRC mismatch"));
    const QByteArray payload = bytes.mid(HeaderSize, payloadLength);
    if (read32(bytes.constData() + 16) != crc32(payload))
        return fail(QStringLiteral("APP1 payload CRC mismatch"));
    frame->type = read16(bytes.constData() + 6);
    frame->sequence = read32(bytes.constData() + 8);
    frame->flags = flags;
    frame->payload = payload;
    if (error) error->clear();
    return true;
}

bool App1Codec::takeFrame(QByteArray *stream, App1Frame *frame, QString *error)
{
    if (!stream || !frame) {
        if (error) *error = QStringLiteral("stream or frame is null");
        return false;
    }
    const QByteArray magic("APP1", 4);
    for (;;) {
        const int magicIndex = stream->indexOf(magic);
        if (magicIndex < 0) {
            if (stream->size() > 3)
                stream->remove(0, stream->size() - 3);
            return false;
        }
        if (magicIndex > 0)
            stream->remove(0, magicIndex);
        if (stream->size() < HeaderSize)
            return false;
        const quint16 payloadLength = read16(stream->constData() + 12);
        if (payloadLength > MaxPayloadSize) {
            stream->remove(0, 1);
            if (error) *error = QStringLiteral("APP1 payload length exceeds limit");
            continue;
        }
        const int frameSize = HeaderSize + payloadLength;
        if (stream->size() < frameSize)
            return false;
        const QByteArray candidate = stream->left(frameSize);
        if (decode(candidate, frame, error)) {
            stream->remove(0, frameSize);
            return true;
        }
        stream->remove(0, 1);
    }
}
