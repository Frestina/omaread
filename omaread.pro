QT += core gui widgets printsupport network quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = omaread
TEMPLATE = app

# The one place the version is written; omaread --version and the shortcuts
# window read it from here.
VERSION = 0.4.6
DEFINES += OMAREAD_VERSION=\\\"$$VERSION\\\"

LIBS += -lcmark-gfm-extensions -lcmark-gfm -lKF6SyntaxHighlighting
INCLUDEPATH += /usr/include/KF6/KSyntaxHighlighting

HEADERS += \
    src/backend.h \
    src/codehighlighter.h \
    src/figures.h \
    src/cli.h \
    src/findhighlighter.h \
    src/markdownrenderer.h \
    src/printdocument.h \
    src/hyprlandrounding.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/codehighlighter.cpp \
    src/figures.cpp \
    src/cli.cpp \
    src/findhighlighter.cpp \
    src/markdownrenderer.cpp \
    src/printdocument.cpp \
    src/hyprlandrounding.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
