#ifndef INTELHEXPARSER_H
#define INTELHEXPARSER_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct FirmwareImage
{
    QByteArray image;
    quint32 crc32 = 0;
    quint32 imageVersion = 0;
    quint32 baseAddress = 0x08020000U;
};

class IntelHexParser
{
public:
    static constexpr quint32 ApplicationBase = 0x08020000U;
    static constexpr quint32 ApplicationSize = 0x000C0000U;

    static bool parse(const QByteArray &hexText, FirmwareImage *image,
                      QString *error);
    static bool parseFile(const QString &path, FirmwareImage *image,
                          QString *error);
};

#endif // INTELHEXPARSER_H
