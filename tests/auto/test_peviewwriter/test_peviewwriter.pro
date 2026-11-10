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
TARGET = test_peviewwriter

QT += core xml testlib

INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/partseditor)

# The Q_OBJECT test class is declared inside test_peviewwriter.cpp (see the comment there), so qmake
# mocs the source and generates the in-source .moc that CONFIG += testcase requires. SOURCES are
# otherwise all $$absolute_path() per the qmake path-keying hazard in AGENTS.md 5 -- a relative
# $$files() self-glob mixed with absolute entries emits duplicate moc object rules and breaks only the
# aggregate -j build, which passes in isolation and so hides itself.
#
# The suite links ONLY the pure-logic DOM-writer and the textutils helpers it needs (svgNSOnly). It
# deliberately does NOT link pesvgeditor.cpp: that pulls in the QGraphicsView/QSvgRenderer widget stack
# (needs QT += widgets svg svgwidgets and a QGuiApplication), which a headless golden unit test stays
# out of. textutils.cpp is self-sufficient on Qt Core/Xml (misc.h/installedfonts.h are header-only),
# exactly as test_pedocument already relies on.
SOURCES += test_peviewwriter.cpp
SOURCES += $$absolute_path(../../../src/partseditor/peviewwriter.cpp)
SOURCES += $$absolute_path(../../../src/utils/textutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textmetrics.cpp)

HEADERS += $$absolute_path(../../../src/partseditor/peviewwriter.h)
HEADERS += $$absolute_path(../../../src/utils/textutils.h)
HEADERS += $$absolute_path(../../../src/utils/textmetrics.h)
