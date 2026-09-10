#include "WinUsbDeviceDiscovery.h"

#include <QRegularExpression>

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usb100.h>

namespace {

const GUID kUpgradeInterfaceGuid = {
    0x6e15414d, 0xb3e8, 0x4b08,
    {0x9b, 0x73, 0x73, 0xdb, 0x7e, 0x6a, 0x0f, 0x40}
};

QString windowsError(const QString &operation)
{
    const DWORD code = GetLastError();
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

QString usbString(WINUSB_INTERFACE_HANDLE handle, UCHAR index)
{
    if (index == 0)
        return {};
    UCHAR language[256] = {};
    ULONG transferred = 0;
    if (!WinUsb_GetDescriptor(handle, USB_STRING_DESCRIPTOR_TYPE, 0, 0,
                              language, sizeof(language), &transferred)
        || transferred < 4)
        return {};
    const USHORT languageId = USHORT(language[2])
        | (USHORT(language[3]) << 8);
    UCHAR buffer[256] = {};
    transferred = 0;
    if (!WinUsb_GetDescriptor(handle, USB_STRING_DESCRIPTOR_TYPE, index,
                              languageId, buffer, sizeof(buffer), &transferred)
        || transferred < 2 || buffer[1] != USB_STRING_DESCRIPTOR_TYPE)
        return {};
    const int byteCount = qMin<int>(buffer[0], int(transferred)) - 2;
    if (byteCount <= 0)
        return {};
    return QString::fromUtf16(
        reinterpret_cast<const ushort *>(buffer + 2), byteCount / 2);
}

void readUsbIdentity(UpgradeDevice *device)
{
    const HANDLE file = CreateFileW(
        reinterpret_cast<LPCWSTR>(device->path.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    if (WinUsb_Initialize(file, &usb)) {
        USB_DEVICE_DESCRIPTOR descriptor = {};
        ULONG transferred = 0;
        if (WinUsb_GetDescriptor(usb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                                 reinterpret_cast<PUCHAR>(&descriptor),
                                 sizeof(descriptor), &transferred)
            && transferred == sizeof(descriptor)) {
            const QString serial = usbString(usb, descriptor.iSerialNumber);
            const QString product = usbString(usb, descriptor.iProduct);
            if (!serial.isEmpty())
                device->serial = serial;
            if (!product.isEmpty())
                device->product = product;
        }
        WinUsb_Free(usb);
    }
    CloseHandle(file);
}

} // namespace

bool WinUsbDeviceDiscovery::parseDevicePath(const QString &path,
                                            UpgradeDevice *device)
{
    if (!device)
        return false;
    static const QRegularExpression expression(
        QStringLiteral(R"(vid_([0-9a-f]{4})&pid_([0-9a-f]{4})(?:&mi_[0-9a-f]{2})?#([^#]+)#)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(path);
    if (!match.hasMatch())
        return false;
    bool vidOk = false;
    bool pidOk = false;
    const quint16 vid = match.captured(1).toUShort(&vidOk, 16);
    const quint16 pid = match.captured(2).toUShort(&pidOk, 16);
    if (!vidOk || !pidOk || vid != 0xCAFEU || pid != 0x4070U)
        return false;
    device->path = path;
    device->vid = vid;
    device->pid = pid;
    device->serial = match.captured(3);
    device->product = QStringLiteral("MYFOC Motor Controller");
    device->mode = UpgradeDevice::UnknownMode;
    device->capabilities = 0U;
    return true;
}

QList<UpgradeDevice> WinUsbDeviceDiscovery::enumerate(QString *error)
{
    if (error)
        error->clear();
    QList<UpgradeDevice> result;
    HDEVINFO set = SetupDiGetClassDevsW(
        &kUpgradeInterfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        if (error)
            *error = windowsError(QStringLiteral("SetupDiGetClassDevs"));
        return result;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(
                set, nullptr, &kUpgradeInterfaceGuid, index, &interfaceData)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS && error)
                *error = windowsError(QStringLiteral("SetupDiEnumDeviceInterfaces"));
            break;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(
            set, &interfaceData, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            continue;
        QByteArray storage(int(required), Qt::Uninitialized);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(
            storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                set, &interfaceData, detail, required, nullptr, nullptr))
            continue;
        UpgradeDevice device;
        if (parseDevicePath(QString::fromWCharArray(detail->DevicePath),
                            &device)) {
            readUsbIdentity(&device);
            result.append(device);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}
