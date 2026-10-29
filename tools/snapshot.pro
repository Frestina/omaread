QT += core gui widgets printsupport network quick quickcontrols2 dbus
CONFIG += c++17
TEMPLATE = app
TARGET = snapshot

LIBS += -lcmark-gfm-extensions -lcmark-gfm

INCLUDEPATH += ../src
SOURCES += \
    snapshot.cpp \
    ../src/backend.cpp \
    ../src/markdownrenderer.cpp \
    ../src/printdocument.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownrenderer.h \
    ../src/printdocument.h

RESOURCES += ../src/resources.qrc
