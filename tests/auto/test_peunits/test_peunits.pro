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
TARGET = test_peunits

QT += core xml testlib

INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/partseditor)

# The real-part golden test reads a bundled part svg; hand it the tree's absolute root at build time.
DEFINES += PE_PARTS_SVG_ROOT=\\\"$$absolute_path(../../../parts/svg)\\\"

# The test class is declared inside test_peunits.cpp (see the comment there), so qmake mocs the
# source and generates the in-source .moc that CONFIG += testcase requires. SOURCES are otherwise all
# $$absolute_path() per the qmake path-keying hazard in AGENTS.md 5 -- a relative $$files() self-glob
# mixed with absolute entries emits duplicate moc object rules and breaks only the aggregate -j build,
# which passes in isolation and so hides itself.
#
# This suite links ONLY peunits.cpp: the R2 units core is pure QtCore (conversion + QSettings gate). It
# deliberately does NOT link pesvgeditor.cpp -- that drags the QGraphicsView widget stack in, which a
# headless golden unit test must stay out of. The editor's wiring of these values into the readout is
# covered by test_pesvgeditor (which does link the widget stack) and by the manual check.
SOURCES += test_peunits.cpp
SOURCES += $$absolute_path(../../../src/partseditor/peunits.cpp)

HEADERS += $$absolute_path(../../../src/partseditor/peunits.h)
