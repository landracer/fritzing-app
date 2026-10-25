/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#ifndef PANELIZER_H
#define PANELIZER_H

#include <QString>
#include <QSizeF>

#include "../mainwindow/mainwindow.h"
#include "../items/itembase.h"
#include "../svg/svg2gerber.h"

#include <QDir>
#include <QFileInfo>

// Describes one Gerber output layer: which view layers feed it, what it is
// for, and its filename suffix. Reused by the FabExporter panel pipeline
// (src/svg/fabexporter.cpp).
struct LayerThing {
	LayerList layerList;
	QString name;
	SVG2gerber::ForWhy forWhy;
	QString suffix;

	LayerThing(const QString & n, LayerList ll, SVG2gerber::ForWhy fw, const QString & s)
        : layerList(ll), name(n), forWhy(fw), suffix(s)
    {
	}
};

// What remains of the former batch panelizer. The panel *layout* is now done
// by panelizerengine.cpp using rbp::GuillotineBinPack (the same packer the
// rest of Fritzing uses); the obsolete corner-stitching/Tile batch pipeline
// has been removed. The single surviving piece is makeSVGs(), a per-board
// per-layer SVG render helper that the wizard-driven FabExporter reuses.
class Panelizer
{
public:
	static void makeSVGs(MainWindow *, ItemBase *, const QString & boardName, QList<LayerThing> & layerThingList, QDir & saveDir, QFileInfo & copyInfo);
};

#endif
