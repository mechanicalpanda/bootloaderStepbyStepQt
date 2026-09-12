#ifndef APP1CODEC_H
#define APP1CODEC_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct App1Frame
{
    quint16 type = 0;
    quint32 sequence = 0;
    quint16 flags = 0;
    QByteArray payload;
};

class App1Codec
{
public:
    enum Type : quint16
    {
        EnterBootloader = 0x0001,
        GetMode = 0x0002,
        GetDeviceInfo = 0x0003,
        GetFirmwareInfo = 0x0004,
        BlHello = 0x0200,
        BlBegin = 0x0201,
        BlData = 0x0202,
        BlEnd = 0x0203,
        BlStatus = 0x0204,
        BlAbort = 0x0205,
        BlReboot = 0x0206,
        BlInstall = 0x0207,
        BlClearError = 0x0208
    };

    enum Mode : quint8
    {
        ApplicationMode = 0x01,
        BootloaderMode = 0x02
    };

    enum Capability : quint32
    {
        CanEnterBootloader = 0x00000001U,
        CanUpgrade = 0x00000002U,
        CanReboot = 0x00000004U,
        CanQueryDeviceInfo = 0x00000008U,
        CanQueryFirmwareInfo = 0x00000010U
    };

    static constexpr quint16 ModeProtocolVersion = 0x0001U;
    static constexpr int HeaderSize = 24;
    static constexpr int MaxPayloadSize = 320;
    static constexpr quint16 ResponseFlag = 0x0001;
    static constexpr quint16 ErrorFlag = 0x0002;

    static quint32 crc32(const QByteArray &data);
    static QByteArray encodeRequest(quint16 type, quint32 sequence,
                                    const QByteArray &payload = {});
    static QByteArray encodeResponse(quint16 type, quint32 sequence, bool error,
                                     const QByteArray &payload = {});
    static bool decode(const QByteArray &bytes, App1Frame *frame, QString *error);
    static bool takeFrame(QByteArray *stream, App1Frame *frame, QString *error);

private:
    static QByteArray encode(quint16 type, quint32 sequence, quint16 flags,
                             const QByteArray &payload);
};

#endif // APP1CODEC_H
