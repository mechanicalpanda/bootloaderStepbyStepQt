#ifndef FIRMWAREUPGRADECONTROLLER_H
#define FIRMWAREUPGRADECONTROLLER_H

#include "App1Codec.h"
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
        EnterBootloader,
        WaitBootloader,
        Hello,
        Begin,
        Transfer,
        End,
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

signals:
    void devicesChanged(const QList<UpgradeDevice> &devices);
    void stageChanged(FirmwareUpgradeController::Stage stage,
                      const QString &description);
    void progressChanged(quint32 acknowledgedBytes, quint32 totalBytes,
                         double bytesPerSecond, int etaSeconds);
    void logMessage(const QString &message);
    void finished(bool success, const QString &message);

private slots:
    void onDataReceived(const QByteArray &data);
    void onTransportError(const QString &message);
    void onDisconnected();
    void onRequestTimeout();
    void scanForTransition();

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
    bool openDevice(const UpgradeDevice &device);
    bool writeRequest(quint16 type, const QByteArray &payload,
                      QString *error);
    void sendRequest(quint16 type, const QByteArray &payload = {});
    bool beginModeProbe(const UpgradeDevice &device, ProbePurpose purpose,
                        bool fatalOpenError);
    void probeNextRefreshDevice();
    void handleModeProbeFailure(const QString &message);
    bool applyModeResponse(const QByteArray &payload, UpgradeDevice *device,
                           QString *error) const;
    void handleResponse(const App1Frame &response);
    void sendHello();
    void sendStatus();
    void sendBegin();
    void sendNextData();
    void sendEnd();
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
    QString m_serial;
    QByteArray m_rxStream;
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
    QTimer m_requestTimer;
    QTimer m_transitionTimer;
    QElapsedTimer m_transitionElapsed;
    QElapsedTimer m_transferElapsed;
};

#endif // FIRMWAREUPGRADECONTROLLER_H
