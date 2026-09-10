#ifndef WINUSBTRANSPORT_H
#define WINUSBTRANSPORT_H

#include "UpgradeTransport.h"

#include <QTimer>

class WinUsbTransport : public UpgradeTransport
{
    Q_OBJECT
public:
    explicit WinUsbTransport(QObject *parent = nullptr);
    ~WinUsbTransport() override;

    QList<UpgradeDevice> discover(QString *error) override;
    bool open(const UpgradeDevice &device, QString *error) override;
    void close() override;
    bool write(const QByteArray &data, QString *error) override;

private slots:
    void pollInput();

private:
    static QString errorText(const QString &operation, unsigned long code);
    void *m_file = reinterpret_cast<void *>(-1);
    void *m_usb = nullptr;
    unsigned char m_bulkOut = 0;
    unsigned char m_bulkIn = 0;
    QTimer m_readTimer;
};

#endif // WINUSBTRANSPORT_H
