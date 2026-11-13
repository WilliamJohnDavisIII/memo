QT += core gui qml quick quickcontrols2 multimedia concurrent

CONFIG += c++17 release
TARGET = memo
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
