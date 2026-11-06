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
TARGET = test_peviewlayermap

QT += core xml testlib

INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/partseditor)

# The test class is declared inside test_peviewlayermap.cpp (see the comment there), so qmake mocs the
# source and generates the in-source .moc that CONFIG += testcase requires. SOURCES are otherwise all
# $$absolute_path() per the qmake path-keying hazard in AGENTS.md 5 -- a relative $$files() self-glob
# mixed with absolute entries emits duplicate moc object rules and breaks only the aggregate -j build,
# which passes in isolation and so hides itself.
SOURCES += test_peviewlayermap.cpp
SOURCES += $$absolute_path(../../../src/partseditor/peviewlayermap.cpp)

# Only the ViewLayer::ViewID enum is used. viewlayer.h is include-standalone (verified) and no ViewLayer
# static member or instance is referenced, so it is deliberately NOT listed in HEADERS: doing so makes
# qmake moc the Q_OBJECT class, and the link then demands ViewLayer's vtable/dtor from viewlayer.cpp,
# which would drag the whole application into a headless unit test.
HEADERS += $$absolute_path(../../../src/partseditor/peviewlayermap.h)

