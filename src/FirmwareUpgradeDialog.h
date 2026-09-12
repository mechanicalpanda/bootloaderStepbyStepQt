#ifndef FIRMWAREUPGRADEDIALOG_H
#define FIRMWAREUPGRADEDIALOG_H

#include "FirmwareUpgradeController.h"
#include "WinUsbTransport.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;

class FirmwareUpgradeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FirmwareUpgradeDialog(QWidget *parent = nullptr);

private slots:
    void browseFirmware();
    void refreshDevices();
    void updateDevices(const QList<UpgradeDevice> &devices);
    void startUpgrade();
    void cancelUpgrade();
    void updateStage(FirmwareUpgradeController::Stage stage,
                     const QString &description);
    void updateProgress(quint32 acknowledged, quint32 total,
                        double bytesPerSecond, int etaSeconds);
    void updateDeviceInformation(const DeviceInfo &device,
                                 const FirmwareInfo &installed,
                                 bool installedValid,
                                 const QString &compatibility,
                                 bool upgradeAllowed,
                                 bool downgrade);
    void upgradeFinished(bool success, const QString &message);
    void appendLog(const QString &message);
    void updateRecoveryStatus(quint8 phase, quint8 activeSlot,
                              quint8 candidateSlot, const QByteArray &packageId,
                              quint32 downloadOffset, quint32 backupOffset,
                              quint32 installOffset, quint16 lastError);

private:
    void setUpgradeActive(bool active);
    static QString formatBytes(quint32 bytes);
    static QString formatFirmwareInfo(const FirmwareInfo &info);

    WinUsbTransport m_transport;
    FirmwareUpgradeController m_controller;
    QList<UpgradeDevice> m_devices;
    FirmwareImage m_image;
    QString m_firmwarePath;

    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLineEdit *m_filePathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QLabel *m_fileSummaryLabel = nullptr;
    QLabel *m_candidateInfoLabel = nullptr;
    QLabel *m_deviceInfoLabel = nullptr;
    QLabel *m_compatibilityLabel = nullptr;
    QCheckBox *m_allowDowngradeCheck = nullptr;
    QLabel *m_phaseLabel = nullptr;
    QLabel *m_recoveryLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_bytesLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_etaLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

#endif // FIRMWAREUPGRADEDIALOG_H
