QT += core gui widgets
CONFIG += c++17 release
TEMPLATE = app
TARGET = MYFOCFirmwareUpdater

INCLUDEPATH += src

SOURCES += src/main.cpp \
           src/App1Codec.cpp \
           src/FirmwareInfo.cpp \
           src/DeviceInfo.cpp \
           src/IntelHexParser.cpp \
           src/UpgradeTransport.cpp \
           src/FirmwareUpgradeController.cpp \
           src/WinUsbDeviceDiscovery.cpp \
           src/WinUsbTransport.cpp \
           src/FirmwareUpgradeDialog.cpp

HEADERS += src/App1Codec.h \
           src/FirmwareInfo.h \
           src/DeviceInfo.h \
           src/IntelHexParser.h \
           src/UpgradeTransport.h \
           src/FirmwareUpgradeController.h \
           src/WinUsbDeviceDiscovery.h \
           src/WinUsbTransport.h \
           src/FirmwareUpgradeDialog.h

win32 {
    LIBS += -lsetupapi -lwinusb
}
