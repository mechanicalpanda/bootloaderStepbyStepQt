#ifndef WINUSBDEVICEDISCOVERY_H
#define WINUSBDEVICEDISCOVERY_H

#include "UpgradeTransport.h"

class WinUsbDeviceDiscovery
{
public:
    static QList<UpgradeDevice> enumerate(QString *error);
    static bool parseDevicePath(const QString &path, UpgradeDevice *device);
};

#endif // WINUSBDEVICEDISCOVERY_H
