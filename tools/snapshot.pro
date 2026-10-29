QT += core gui widgets printsupport network quick quickcontrols2 dbus
CONFIG += c++17
TEMPLATE = app
TARGET = snapshot

LIBS += -lcmark-gfm-extensions -lcmark-gfm -lKF6SyntaxHighlighting
INCLUDEPATH += /usr/include/KF6/KSyntaxHighlighting

INCLUDEPATH += ../src
SOURCES += \
    snapshot.cpp \
    ../src/backend.cpp \
    ../src/codehighlighter.cpp \
    ../src/findhighlighter.cpp \
    ../src/markdownrenderer.cpp \
    ../src/printdocument.cpp
HEADERS += \
    ../src/backend.h \
    ../src/codehighlighter.h \
    ../src/findhighlighter.h \
    ../src/markdownrenderer.h \
    ../src/printdocument.h

RESOURCES += ../src/resources.qrc
