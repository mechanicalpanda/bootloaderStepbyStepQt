#ifndef FIRMWAREUPGRADECONTROLLER_H
#define FIRMWAREUPGRADECONTROLLER_H

#include "App1Codec.h"
#include "DeviceInfo.h"
#include "IntelHexParser.h"
#include "UpgradeTransport.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class FirmwareUpgradeController : public QObject
{
    Q_OBJECT
public:
    enum Stage
    {
        Idle,
        ValidateFile,
        QueryMode,
        QueryDeviceInformation,
        QueryFirmwareInformation,
        EnterBootloader,
        WaitBootloader,
        Hello,
        Status,
        BeginStaging,
        TransferToExternal,
        EndStaging,
        Install,
        MonitorRecovery,
        WaitApplication,
        Completed,
        Failed,
        Cancelled
    };
    Q_ENUM(Stage)

    explicit FirmwareUpgradeController(UpgradeTransport *transport,
                                       QObject *parent = nullptr);

    Stage stage() const { return m_stage; }
    bool isActive() const;
    void refreshDevices();
    void startUpgrade(const UpgradeDevice &device, const FirmwareImage &image);
    void cancel();
    void setRequestTimeout(int milliseconds);
    void setAllowDowngrade(bool allow) { m_allowDowngrade = allow; }

    static bool checkCompatibility(const FirmwareInfo &candidate,
                                   const DeviceInfo &device,
                                   const FirmwareInfo *installed,
                                   bool allowDowngrade,
                                   QString *description,
                                   bool *downgrade);

signals:
    void devicesChanged(const QList<UpgradeDevice> &devices);
    void stageChanged(FirmwareUpgradeController::Stage stage,
                      const QString &description);
    void progressChanged(quint32 acknowledgedBytes, quint32 totalBytes,
                         double bytesPerSecond, int etaSeconds);
    void deviceInformationChanged(const DeviceInfo &device,
                                  const FirmwareInfo &installed,
                                  bool installedValid,
                                  const QString &compatibility,
                                  bool upgradeAllowed,
                                  bool downgrade);
    void logMessage(const QString &message);
    void recoveryStatusChanged(quint8 phase, quint8 activeSlot,
                               quint8 candidateSlot, const QByteArray &packageId,
                               quint32 downloadOffset, quint32 backupOffset,
                               quint32 installOffset, quint16 lastError);
    void finished(bool success, const QString &message);

private slots:
    void onDataReceived(const QByteArray &data);
    void onTransportError(const QString &message);
    void onDisconnected();
    void onRequestTimeout();
    void scanForTransition();
    void pollRecovery();

private:
    enum ProbePurpose
    {
        NoProbe,
        RefreshProbe,
        StartProbe,
        WaitBootloaderProbe,
        WaitApplicationProbe
    };

    void setStage(Stage stage, const QString &description);
    void fail(const QString &message);
    bool writeRequest(quint16 type, const QByteArray &payload,
                      QString *error);
    void sendRequest(quint16 type, const QByteArray &payload = {});
    bool beginModeProbe(const UpgradeDevice &device, ProbePurpose purpose,
                        bool fatalOpenError);
    void probeNextRefreshDevice();
    void handleProbeFailure(const QString &message);
    bool applyModeResponse(const QByteArray &payload, UpgradeDevice *device,
                           QString *error) const;
    void handleResponse(const App1Frame &response);
    void finishInformationProbe(bool installedValid);
    void sendHello();
    void sendStatus();
    void sendBegin();
    void sendNextData();
    void sendEnd();
    void sendInstall();
    void updateProgress(quint32 acknowledged);
    static quint16 read16(const QByteArray &data, int offset);
    static quint32 read32(const QByteArray &data, int offset);
    static void append16(QByteArray &data, quint16 value);
    static void append32(QByteArray &data, quint32 value);

    UpgradeTransport *m_transport;
    Stage m_stage = Idle;
    UpgradeDevice m_initialDevice;
    UpgradeDevice m_probeDevice;
    QList<UpgradeDevice> m_refreshCandidates;
    QList<UpgradeDevice> m_refreshResolved;
    FirmwareImage m_image;
    DeviceInfo m_deviceInfo;
    FirmwareInfo m_installedFirmware;
    QString m_serial;
    QByteArray m_rxStream;
    QByteArray m_packageId;
    QByteArray m_pendingFrame;
    quint16 m_pendingType = 0;
    quint32 m_pendingSequence = 0;
    quint32 m_sequence = 0;
    quint32 m_offset = 0;
    quint32 m_sentEnd = 0;
    quint32 m_transferStartOffset = 0;
    quint16 m_blockSize = 240;
    int m_retryCount = 0;
    int m_refreshIndex = 0;
    int m_requestTimeoutMs = 1000;
    ProbePurpose m_probePurpose = NoProbe;
    bool m_allowDowngrade = false;
    bool m_installedFirmwareValid = false;
    QTimer m_requestTimer;
    QTimer m_transitionTimer;
    QTimer m_recoveryTimer;
    QElapsedTimer m_transitionElapsed;
    QElapsedTimer m_transferElapsed;
};

Q_DECLARE_METATYPE(DeviceInfo)
Q_DECLARE_METATYPE(FirmwareInfo)

#endif // FIRMWAREUPGRADECONTROLLER_H
