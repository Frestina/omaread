QT += core gui quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = omaread
TEMPLATE = app

LIBS += -lcmark-gfm-extensions -lcmark-gfm

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/markdownrenderer.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/markdownrenderer.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
