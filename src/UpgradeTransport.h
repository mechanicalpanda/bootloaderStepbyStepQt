#ifndef UPGRADETRANSPORT_H
#define UPGRADETRANSPORT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

struct UpgradeDevice
{
    enum Mode
    {
        UnknownMode,
        ApplicationMode,
        BootloaderMode
    };

    QString path;
    QString serial;
    QString product;
    quint16 vid = 0;
    quint16 pid = 0;
    Mode mode = UnknownMode;
    quint32 capabilities = 0;

    QString displayName() const;
    bool isApplication() const { return mode == ApplicationMode; }
    bool isBootloader() const { return mode == BootloaderMode; }
};
Q_DECLARE_METATYPE(UpgradeDevice)
Q_DECLARE_METATYPE(QList<UpgradeDevice>)

class UpgradeTransport : public QObject
{
    Q_OBJECT
public:
    explicit UpgradeTransport(QObject *parent = nullptr);
    ~UpgradeTransport() override;

    virtual QList<UpgradeDevice> discover(QString *error) = 0;
    virtual bool open(const UpgradeDevice &device, QString *error) = 0;
    virtual void close() = 0;
    virtual bool write(const QByteArray &data, QString *error) = 0;

signals:
    void dataReceived(const QByteArray &data);
    void disconnected();
    void transportError(const QString &message);
};

#endif // UPGRADETRANSPORT_H
