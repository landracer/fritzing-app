# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2019 Fritzing
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
include($$absolute_path(../../../pri/svgppdetect.pri))

QT += core xml svg
equals(QT_MAJOR_VERSION, 6) {
  QT += core5compat svgwidgets
}

# Note: list sources explicitly (no $$files() glob of this dir). Mixing a relative
# $$files(*.h) self-glob with absolute src/... entries made qmake emit BOTH a relative- and an
# absolute-path-keyed rule for each moc_<x>.o => the Q_OBJECT meta-object TU compiles to an empty
# object in the aggregate build => "undefined reference to main". Explicit, single-sourced absolute
# entries (matching tests/auto/test_gerber/test_gerber.pro) produce exactly one rule per source.
INCLUDEPATH += $$absolute_path(../../../src)

HEADERS += $$absolute_path(../../../src/svg/svgtext.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathlexer.h)
HEADERS += $$absolute_path(../../../src/utils/textutils.h)
HEADERS += $$absolute_path(../../../src/utils/textmetrics.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathgrammar_p.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathparser.h)

SOURCES += $$absolute_path(test_mfinder.cpp)
SOURCES += $$absolute_path(test_pathlexer_protected.cpp)
SOURCES += $$absolute_path(test_pathlexer_public.cpp)
SOURCES += $$absolute_path(test_pathparse.cpp)
SOURCES += $$absolute_path(test_svgtext.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgtext.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathlexer.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathparser.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathgrammar.cpp)
SOURCES += $$absolute_path(../../../src/utils/textutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textmetrics.cpp)
#INCLUDEPATH += $$top_srcdir
# unix:QMAKE_POST_LINK = $$PWD/generated/test_svg
