QT += core gui quick quickcontrols2 multimedia concurrent testlib
CONFIG += c++17 testcase
TARGET = memo_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/transcriber.h ../src/backend.h ../src/theme.h ../src/translator.h
SOURCES += memo_tests.cpp ../src/transcriber.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp
