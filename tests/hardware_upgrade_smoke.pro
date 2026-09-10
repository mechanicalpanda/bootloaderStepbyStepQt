QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = hardware_upgrade_smoke
INCLUDEPATH += ../src
SOURCES += hardware_upgrade_smoke.cpp \
           ../src/FirmwareInfo.cpp \
           ../src/DeviceInfo.cpp \
           ../src/IntelHexParser.cpp \
           ../src/App1Codec.cpp \
           ../src/FirmwareUpgradeController.cpp \
           ../src/UpgradeTransport.cpp \
           ../src/WinUsbDeviceDiscovery.cpp \
           ../src/WinUsbTransport.cpp
HEADERS += ../src/FirmwareInfo.h \
           ../src/DeviceInfo.h \
           ../src/IntelHexParser.h \
           ../src/App1Codec.h \
           ../src/FirmwareUpgradeController.h \
           ../src/UpgradeTransport.h \
           ../src/WinUsbDeviceDiscovery.h \
           ../src/WinUsbTransport.h

LIBS += -lsetupapi -lwinusb
