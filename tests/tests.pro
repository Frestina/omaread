QT += core gui widgets printsupport network quick dbus testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omaread

LIBS += -lcmark-gfm-extensions -lcmark-gfm

INCLUDEPATH += ../src
SOURCES += \
    tst_omaread.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/findhighlighter.cpp \
    ../src/markdownrenderer.cpp \
    ../src/printdocument.cpp
HEADERS += \
    ../src/backend.h \
    ../src/cli.h \
    ../src/findhighlighter.h \
    ../src/markdownrenderer.h \
    ../src/printdocument.h
