QT += core gui testlib dbus qml quick quickcontrols2
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omacalc

INCLUDEPATH += ../src
SOURCES += \
    tst_omacalc.cpp \
    ../src/backend.cpp
HEADERS += \
    ../src/backend.h

RESOURCES += ../src/resources.qrc
