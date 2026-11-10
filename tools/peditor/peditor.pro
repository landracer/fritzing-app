QT += widgets svg svgwidgets xml
CONFIG += c++17
TARGET = peditor
INCLUDEPATH += $$PWD/../../src/partseditor
SOURCES += main.cpp \
           $$PWD/../../src/partseditor/pesvgeditor.cpp \
           $$PWD/../../src/partseditor/pesvgpartloader.cpp \
           $$PWD/../../src/partseditor/peprimitivegeom.cpp
HEADERS += $$PWD/../../src/partseditor/pesvgeditor.h \
           $$PWD/../../src/partseditor/pesvgpartloader.h \
           $$PWD/../../src/partseditor/peprimitivegeom.h
