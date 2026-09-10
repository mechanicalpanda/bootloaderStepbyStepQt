#include "WinUsbTransport.h"

#include "WinUsbDeviceDiscovery.h"

#include <windows.h>
#include <winusb.h>
#include <usb100.h>

WinUsbTransport::WinUsbTransport(QObject *parent)
    : UpgradeTransport(parent)
{
    m_readTimer.setInterval(5);
    connect(&m_readTimer, &QTimer::timeout,
            this, &WinUsbTransport::pollInput);
}

WinUsbTransport::~WinUsbTransport()
{
    close();
}

QList<UpgradeDevice> WinUsbTransport::discover(QString *error)
{
    return WinUsbDeviceDiscovery::enumerate(error);
}

QString WinUsbTransport::errorText(const QString &operation,
                                   unsigned long code)
{
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
                       | FORMAT_MESSAGE_FROM_SYSTEM
                       | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0,
                   reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const QString detail = message
        ? QString::fromWCharArray(message).trimmed()
        : QStringLiteral("unknown error");
    if (message)
        LocalFree(message);
    return QStringLiteral("%1 failed (%2): %3")
        .arg(operation).arg(code).arg(detail);
}

bool WinUsbTransport::open(const UpgradeDevice &device, QString *error)
{
    close();
    if (error)
        error->clear();
    HANDLE file = CreateFileW(
        reinterpret_cast<LPCWSTR>(device.path.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error)
            *error = errorText(QStringLiteral("Opening WinUSB device"),
                               GetLastError());
        return false;
    }
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    if (!WinUsb_Initialize(file, &usb)) {
        const DWORD code = GetLastError();
        CloseHandle(file);
        if (error)
            *error = errorText(QStringLiteral("WinUsb_Initialize"), code);
        return false;
    }

    USB_INTERFACE_DESCRIPTOR descriptor = {};
    if (!WinUsb_QueryInterfaceSettings(usb, 0, &descriptor)) {
        const DWORD code = GetLastError();
        WinUsb_Free(usb);
        CloseHandle(file);
        if (error)
            *error = errorText(QStringLiteral("WinUsb_QueryInterfaceSettings"),
                               code);
        return false;
    }
    UCHAR bulkOut = 0;
    UCHAR bulkIn = 0;
    for (UCHAR index = 0; index < descriptor.bNumEndpoints; ++index) {
        WINUSB_PIPE_INFORMATION pipe = {};
        if (!WinUsb_QueryPipe(usb, 0, index, &pipe))
            continue;
        if (pipe.PipeType != UsbdPipeTypeBulk)
            continue;
        if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId))
            bulkIn = pipe.PipeId;
        else
            bulkOut = pipe.PipeId;
    }
    if (bulkOut != 0x01U || bulkIn != 0x81U) {
        WinUsb_Free(usb);
        CloseHandle(file);
        if (error)
            *error = QStringLiteral(
                "The WinUSB interface does not expose Bulk OUT 0x01 and IN 0x81.");
        return false;
    }

    ULONG readTimeout = 1;
    ULONG writeTimeout = 1000;
    WinUsb_SetPipePolicy(usb, bulkIn, PIPE_TRANSFER_TIMEOUT,
                         sizeof(readTimeout), &readTimeout);
    WinUsb_SetPipePolicy(usb, bulkOut, PIPE_TRANSFER_TIMEOUT,
                         sizeof(writeTimeout), &writeTimeout);
    m_file = file;
    m_usb = usb;
    m_bulkOut = bulkOut;
    m_bulkIn = bulkIn;
    m_readTimer.start();
    return true;
}

void WinUsbTransport::close()
{
    m_readTimer.stop();
    if (m_usb) {
        WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(m_usb));
        m_usb = nullptr;
    }
    HANDLE file = static_cast<HANDLE>(m_file);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        m_file = INVALID_HANDLE_VALUE;
    }
    m_bulkOut = 0;
    m_bulkIn = 0;
}

bool WinUsbTransport::write(const QByteArray &data, QString *error)
{
    if (error)
        error->clear();
    if (!m_usb || m_bulkOut == 0U) {
        if (error)
            *error = QStringLiteral("No WinUSB device is open.");
        return false;
    }
    ULONG transferred = 0;
    if (!WinUsb_WritePipe(
            static_cast<WINUSB_INTERFACE_HANDLE>(m_usb), m_bulkOut,
            reinterpret_cast<PUCHAR>(
                const_cast<char *>(data.constData())),
            ULONG(data.size()), &transferred, nullptr)) {
        if (error)
            *error = errorText(QStringLiteral("WinUSB Bulk OUT"),
                               GetLastError());
        return false;
    }
    if (transferred != ULONG(data.size())) {
        if (error)
            *error = QStringLiteral("WinUSB Bulk OUT wrote only %1 of %2 bytes.")
                .arg(transferred).arg(data.size());
        return false;
    }
    return true;
}

void WinUsbTransport::pollInput()
{
    if (!m_usb || m_bulkIn == 0U)
        return;
    unsigned char buffer[512] = {};
    ULONG transferred = 0;
    if (WinUsb_ReadPipe(
            static_cast<WINUSB_INTERFACE_HANDLE>(m_usb), m_bulkIn,
            buffer, sizeof(buffer), &transferred, nullptr)) {
        if (transferred != 0U)
            emit dataReceived(QByteArray(
                reinterpret_cast<const char *>(buffer), int(transferred)));
        return;
    }
    const DWORD code = GetLastError();
    if (code == ERROR_SEM_TIMEOUT || code == ERROR_IO_PENDING
        || code == ERROR_NO_MORE_ITEMS)
        return;
    close();
    if (code == ERROR_DEVICE_NOT_CONNECTED
        || code == ERROR_GEN_FAILURE || code == ERROR_INVALID_HANDLE)
        emit disconnected();
    else
        emit transportError(errorText(QStringLiteral("WinUSB Bulk IN"), code));
}
