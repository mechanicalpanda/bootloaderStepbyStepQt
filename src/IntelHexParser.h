#ifndef INTELHEXPARSER_H
#define INTELHEXPARSER_H

#include "FirmwareInfo.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct FirmwareImage
{
    QByteArray image;
    quint32 crc32 = 0;
    quint32 baseAddress = 0x08020000U;
    FirmwareInfo firmwareInfo;
    bool encrypted = false;
    QByteArray aesIv;
    QByteArray packageId;
};

class IntelHexParser
{
public:
    static constexpr quint32 ApplicationBase = 0x08020000U;
    static constexpr quint32 VectorBase = 0x08020200U;
    static constexpr quint32 CodeBase = 0x08020400U;
    static constexpr quint32 ApplicationSize = 0x000C0000U;

    static bool parse(const QByteArray &hexText, FirmwareImage *image,
                      QString *error);
    static bool parseFile(const QString &path, FirmwareImage *image,
                          QString *error);
};

#endif // INTELHEXPARSER_H

