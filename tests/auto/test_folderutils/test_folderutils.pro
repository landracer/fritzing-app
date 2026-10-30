# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2021,2026 Fritzing
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

QT += core

# Note: list sources explicitly (no $$files() glob of this dir), matching test_gerber.pro and the
# other suites: a relative self-glob mixed with absolute src/... entries can make qmake emit
# duplicate/mis-keyed moc_<x>.o rules in the aggregate build. Explicit, single-sourced absolute
# entries produce exactly one rule per source.
INCLUDEPATH += $$absolute_path(../../../src)
INCLUDEPATH += $$absolute_path(../../../src/utils)

HEADERS += $$absolute_path(../../../src/utils/sanitizeforpath.h)
SOURCES += $$absolute_path(test_folderutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/sanitizeforpath.cpp)
