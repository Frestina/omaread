QT += core gui widgets printsupport network quick quickcontrols2 dbus testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omaread

LIBS += -lcmark-gfm-extensions -lcmark-gfm -lKF6SyntaxHighlighting
INCLUDEPATH += /usr/include/KF6/KSyntaxHighlighting

INCLUDEPATH += ../src
SOURCES += \
    tst_omaread.cpp \
    ../src/backend.cpp \
    ../src/codehighlighter.cpp \
    ../src/figures.cpp \
    ../src/cli.cpp \
    ../src/findhighlighter.cpp \
    ../src/markdownrenderer.cpp \
    ../src/printdocument.cpp
HEADERS += \
    ../src/backend.h \
    ../src/codehighlighter.h \
    ../src/figures.h \
    ../src/cli.h \
    ../src/findhighlighter.h \
    ../src/markdownrenderer.h \
    ../src/printdocument.h
RESOURCES += ../src/resources.qrc
