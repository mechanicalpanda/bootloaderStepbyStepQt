#include "UpgradeTransport.h"

QString UpgradeDevice::displayName() const
{
    QString modeText = QStringLiteral("Unknown");
    if (isApplication())
        modeText = QStringLiteral("Application");
    else if (isBootloader())
        modeText = QStringLiteral("Bootloader");
    return QStringLiteral("%1  [%2]  %3")
        .arg(product.isEmpty() ? QStringLiteral("MYFOC Device") : product,
             serial.isEmpty() ? QStringLiteral("no serial") : serial,
             modeText);
}

UpgradeTransport::UpgradeTransport(QObject *parent)
    : QObject(parent)
{
}

UpgradeTransport::~UpgradeTransport() = default;
