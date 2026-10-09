QT += core gui testlib dbus
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omacalc

INCLUDEPATH += ../src
SOURCES += \
    tst_omacalc.cpp \
    ../src/backend.cpp \
    ../src/systemtheme.cpp
HEADERS += \
    ../src/backend.h \
    ../src/systemtheme.h
