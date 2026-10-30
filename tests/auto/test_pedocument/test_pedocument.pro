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

# specify absolute path so that unit test compiles will find the folder
absolute_boost = 1
include($$absolute_path(../../../pri/boostdetect.pri))

QT += core xml

# List sources explicitly (no $$files() glob of this dir) to avoid the qmake
# path-keying hazard (AGENTS.md 5). pedocument.* / pepath.* have no Q_OBJECT, but we stay
# consistent with every other suite.
INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/partseditor)

HEADERS += $$absolute_path(../../../src/partseditor/pedocument.h)
HEADERS += $$absolute_path(../../../src/partseditor/pepath.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathlexer.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathparser.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathgrammar_p.h)
HEADERS += $$absolute_path(../../../src/utils/textutils.h)
HEADERS += $$absolute_path(../../../src/utils/textmetrics.h)
SOURCES += $$absolute_path(test_pedocument.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/pedocument.cpp)
SOURCES += $$absolute_path(../../../src/partseditor/pepath.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathlexer.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathparser.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathgrammar.cpp)
SOURCES += $$absolute_path(../../../src/utils/textutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textmetrics.cpp)
