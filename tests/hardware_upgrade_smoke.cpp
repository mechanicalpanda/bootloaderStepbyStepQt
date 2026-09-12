#include "FirmwareUpgradeController.h"
#include "IntelHexParser.h"
#include "WinUsbTransport.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() != 2) {
        output << "usage: hardware_upgrade_smoke <firmware.hex>\n";
        return 64;
    }

    FirmwareImage image;
    QString error;
    if (!IntelHexParser::parseFile(application.arguments().at(1),
                                   &image, &error)) {
        output << "HEX_ERROR: " << error << "\n";
        return 65;
    }
    output << "HEX_OK version=" << image.firmwareInfo.versionString()
           << " build=" << image.firmwareInfo.buildNumber
           << " git=" << image.firmwareInfo.gitCommit
           << " size=" << image.image.size()
           << " crc32=" << QStringLiteral("%1").arg(
                  image.crc32, 8, 16, QLatin1Char('0')).toUpper()
           << "\n";
    output.flush();

    WinUsbTransport transport;
    FirmwareUpgradeController controller(&transport);
    bool started = false;
    QObject::connect(&controller, &FirmwareUpgradeController::stageChanged,
                     [&output](FirmwareUpgradeController::Stage stage,
                               const QString &description) {
        output << "STAGE " << int(stage) << ": " << description << "\n";
        output.flush();
    });
    QObject::connect(&controller, &FirmwareUpgradeController::logMessage,
                     [&output](const QString &message) {
        output << "LOG: " << message << "\n";
        output.flush();
    });
    QObject::connect(
        &controller, &FirmwareUpgradeController::deviceInformationChanged,
        [&output](const DeviceInfo &device, const FirmwareInfo &installed,
                  bool installedValid, const QString &compatibility,
                  bool allowed, bool downgrade) {
        output << "DEVICE product="
               << QStringLiteral("%1").arg(
                      device.productId, 8, 16, QLatin1Char('0')).toUpper()
               << " hw=" << device.hardwareRevision
               << " uid=" << device.uniqueId.toHex().toUpper()
               << " installed="
               << (installedValid ? installed.versionString()
                                  : QStringLiteral("none"))
               << " compatibility=" << compatibility
               << " allowed=" << allowed
               << " downgrade=" << downgrade << "\n";
        output.flush();
    });
    QObject::connect(
        &controller, &FirmwareUpgradeController::devicesChanged,
        [&controller, &image, &output, &started](
            const QList<UpgradeDevice> &devices) {
        if (started)
            return;
        if (devices.isEmpty()) {
            output << "DEVICE_ERROR: no MYFOC WinUSB device\n";
            output.flush();
            QCoreApplication::exit(66);
            return;
        }
        started = true;
        output << "DEVICE_FOUND: " << devices.first().displayName() << "\n";
        output.flush();
        controller.startUpgrade(devices.first(), image);
    });
    QObject::connect(
        &controller, &FirmwareUpgradeController::finished,
        [&output](bool success, const QString &message) {
        output << (success ? "UPGRADE_OK: " : "UPGRADE_ERROR: ")
               << message << "\n";
        output.flush();
        QCoreApplication::exit(success ? 0 : 67);
    });
    QTimer::singleShot(90000, [] {
        QTextStream(stderr) << "UPGRADE_ERROR: overall timeout\n";
        QCoreApplication::exit(68);
    });
    QTimer::singleShot(0, &controller,
                       &FirmwareUpgradeController::refreshDevices);
    return application.exec();
}
