QT += core testlib
CONFIG += console c++17 testcase
TEMPLATE = app
TARGET = firmware_info_tests
INCLUDEPATH += ../src
SOURCES += tst_firmwareinfo.cpp \
           ../src/FirmwareInfo.cpp \
           ../src/DeviceInfo.cpp \
           ../src/IntelHexParser.cpp \
           ../src/App1Codec.cpp
HEADERS += ../src/FirmwareInfo.h \
           ../src/DeviceInfo.h \
           ../src/IntelHexParser.h \
           ../src/App1Codec.h
