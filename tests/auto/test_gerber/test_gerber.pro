# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2021 Fritzing GmbH
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

QT += core xml svg gui widgets concurrent core gui network printsupport serialport sql svg widgets xml
equals(QT_MAJOR_VERSION, 6) {
  QT += core5compat svgwidgets
}

# Note: list sources explicitly (no $$files() glob of this dir). A self-glob of *.h plus
# explicit absolute src/svg/*.h entries made qmake emit BOTH a relative- and an
# absolute-path-keyed rule for each moc_<x>.o, so every Q_OBJECT meta-object was compiled
# and linked twice -> "multiple definition of ...::qt_metacall". Explicit, single-sourced
# entries (matching tests/auto/test_svg2gerber/test_svg2gerber.pro, which links cleanly)
# produce exactly one moc rule per header.
HEADERS += $$absolute_path(../../../src/svg/svg2gerber.h)
HEADERS += $$absolute_path(../../../src/svg/svgfilesplitter.h)
HEADERS += $$absolute_path(../../../src/svg/svgflattener.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathgrammar_p.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathlexer.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathparser.h)
HEADERS += $$absolute_path(../../../src/svg/svgpathrunner.h)
HEADERS += $$absolute_path(../../../src/svg/svgtext.h)
HEADERS += $$absolute_path(../../../src/utils/graphicsutils.h)
HEADERS += $$absolute_path(../../../src/utils/textutils.h)
HEADERS += $$absolute_path(../../../src/utils/textmetrics.h)
HEADERS += $$absolute_path(../../../src/debugdialog.h)

SOURCES += test_svg2gerber.cpp
SOURCES += $$absolute_path(../../../src/svg/svg2gerber.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgfilesplitter.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgflattener.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgtext.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathlexer.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathparser.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathgrammar.cpp)
SOURCES += $$absolute_path(../../../src/svg/svgpathrunner.cpp)
SOURCES += $$absolute_path(../../../src/utils/graphicsutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textutils.cpp)
SOURCES += $$absolute_path(../../../src/utils/textmetrics.cpp)
SOURCES += $$absolute_path(../../../src/debugdialog.cpp)

INCLUDEPATH += $$absolute_path(../../../src)

