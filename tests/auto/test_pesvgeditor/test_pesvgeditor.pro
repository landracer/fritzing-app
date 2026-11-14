# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2026 Fritzing
# Fritzing is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Fritzing is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with Fritzing. If not, see <http://www.gnu.org/licenses/>.
# ********************************************************************/

CONFIG += c++17
CONFIG += testcase
TARGET = test_pesvgeditor

QT += core xml testlib widgets

INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/partseditor)
INCLUDEPATH += $$absolute_path(../../../src/utils)

# The Q_OBJECT test class is declared inside test_pesvgeditor.cpp (see the comment there), so qmake
# mocs the source and generates the in-source .moc that CONFIG += testcase requires. SOURCES are
# otherwise all $$absolute_path() per the qmake path-keying hazard in AGENTS.md 5 -- a relative
# $$files() self-glob mixed with absolute entries emits duplicate moc object rules and breaks only the
# aggregate -j build, which passes in isolation and so hides itself.
#
# The suite links ONLY the pure-logic units the editor's engine is built from (the colouriser, the
# per-kind geometry helpers, and the pure itemization planner). It deliberately does NOT link
# pesvgeditor.cpp: that pulls in the QGraphicsView/QSvgRenderer widget stack (needs QT += widgets svg
# svgwidgets and a QGuiApplication), which a headless golden unit test must stay out of. The interactive
# layer is instead verified by the standalone tools/peditor app.
#
# UPDATE (Slice D): pesvgeditor.cpp IS now linked. It no longer pulls QtSvg (the self-paint path is
# QGraphicsObject + QPainterPath only), and this suite already links the widgets stack (it raster-renders the
# self-paint item under QTEST_MAIN). Linking the editor lets the Slice D grip/anchor interaction be golden-tested
# by driving handlePress/handleMove/handleRelease against a real PESvgEditor, not just the pure PEPrimGeom ops.
SOURCES += test_pesvgeditor.cpp
SOURCES += $$absolute_path(../../../src/partseditor/pesvgpartloader.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/peprimitivegeom.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/peitemplan.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/petransform.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/peselfpaintitem.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/pesvgeditor.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/pepath.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/peunits.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathlexer.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathgrammar.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathparser.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathrunner.cpp)
SOURCES += $$absolute_path(../../../src/utils/textutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textmetrics.cpp)
SOURCES += $$absolute_path(../../../src/utils/misc.cpp)

HEADERS += $$absolute_path(../../../src/partseditor/pesvgpartloader.h)
HEADERS += $$absolute_path(../../../src/partseditor/peprimitivegeom.h)
HEADERS += $$absolute_path(../../../src/partseditor/peitemplan.h)
HEADERS += $$absolute_path(../../../src/partseditor/petransform.h)
HEADERS += $$absolute_path(../../../src/partseditor/peselfpaintitem.h)
HEADERS += $$absolute_path(../../../src/partseditor/pesvgeditor.h)
HEADERS += $$absolute_path(../../../src/partseditor/pepath.h)
HEADERS += $$absolute_path(../../../src/partseditor/peunits.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathlexer.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathgrammar_p.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathparser.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathrunner.h)
HEADERS += $$absolute_path(../../../src/utils/textutils.h)
HEADERS += $$absolute_path(../../../src/utils/textmetrics.h)
HEADERS += $$absolute_path(../../../src/utils/misc.h)
HEADERS += $$absolute_path(../../../src/installedfonts.h)
