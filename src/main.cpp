#include "FirmwareUpgradeDialog.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("MYFOC Firmware Updater"));
    application.setOrganizationName(QStringLiteral("MYFOC"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    FirmwareUpgradeDialog dialog;
    dialog.show();
    return application.exec();
}
