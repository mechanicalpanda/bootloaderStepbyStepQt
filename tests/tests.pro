QT += core gui widgets testlib
CONFIG += console c++17 testcase
TEMPLATE = app
TARGET = firmware_core_tests
INCLUDEPATH += ../src
SOURCES += tst_firmwarecore.cpp \
           ../src/FirmwareInfo.cpp \
           ../src/DeviceInfo.cpp \
           ../src/IntelHexParser.cpp \
           ../src/App1Codec.cpp \
           ../src/FirmwareUpgradeController.cpp \
           ../src/UpgradeTransport.cpp \
           ../src/WinUsbDeviceDiscovery.cpp \
           ../src/WinUsbTransport.cpp \
           ../src/FirmwareUpgradeDialog.cpp
HEADERS += ../src/FirmwareInfo.h \
           ../src/DeviceInfo.h \
           ../src/IntelHexParser.h \
           ../src/App1Codec.h \
           ../src/FirmwareUpgradeController.h \
           ../src/UpgradeTransport.h \
           ../src/WinUsbDeviceDiscovery.h \
           ../src/WinUsbTransport.h \
           ../src/FirmwareUpgradeDialog.h

LIBS += -lsetupapi -lwinusb
