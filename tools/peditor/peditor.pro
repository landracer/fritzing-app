QT += widgets svg svgwidgets xml
CONFIG += c++17
TARGET = peditor
INCLUDEPATH += $$PWD/../../src/partseditor
INCLUDEPATH += $$PWD/../../src/utils
SOURCES += main.cpp \
           $$PWD/../../src/partseditor/pesvgeditor.cpp \
           $$PWD/../../src/partseditor/pesvgpartloader.cpp \
           $$PWD/../../src/partseditor/peprimitivegeom.cpp \
           $$PWD/../../src/partseditor/peitemplan.cpp \
           $$PWD/../../src/partseditor/petransform.cpp \
           $$PWD/../../src/partseditor/peselfpaintitem.cpp \
           $$PWD/../../src/partseditor/pepath.cpp \
           $$PWD/../../src/partseditor/peunits.cpp \
           $$PWD/../../src/svg/svgpathlexer.cpp \
           $$PWD/../../src/svg/svgpathgrammar.cpp \
           $$PWD/../../src/svg/svgpathparser.cpp \
           $$PWD/../../src/svg/svgpathrunner.cpp \
           $$PWD/../../src/utils/textutils.cpp \
           $$PWD/../../src/utils/textmetrics.cpp \
           $$PWD/../../src/utils/misc.cpp
HEADERS += $$PWD/../../src/partseditor/pesvgeditor.h \
           $$PWD/../../src/partseditor/pesvgpartloader.h \
           $$PWD/../../src/partseditor/peprimitivegeom.h \
           $$PWD/../../src/partseditor/peitemplan.h \
           $$PWD/../../src/partseditor/petransform.h \
           $$PWD/../../src/partseditor/peselfpaintitem.h \
           $$PWD/../../src/partseditor/pepath.h \
           $$PWD/../../src/partseditor/peunits.h \
           $$PWD/../../src/svg/svgpathlexer.h \
           $$PWD/../../src/svg/svgpathgrammar_p.h \
           $$PWD/../../src/svg/svgpathparser.h \
           $$PWD/../../src/svg/svgpathrunner.h \
           $$PWD/../../src/utils/textutils.h \
           $$PWD/../../src/utils/textmetrics.h \
           $$PWD/../../src/utils/misc.h \
           $$PWD/../../src/installedfonts.h
