#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class DeviceInfo
{
public:
    static constexpr int PayloadSize = 28;

    quint8 mode = 0U;
    quint32 productId = 0U;
    quint16 hardwareRevision = 0U;
    quint32 capabilities = 0U;
    QByteArray uniqueId;

    static bool decode(const QByteArray &bytes, DeviceInfo *info,
                       QString *error);
};

#endif // DEVICEINFO_H
