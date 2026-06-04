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

// What remains of the legacy batch panelizer. The panel *layout* is now
// performed by panelizerengine.cpp using rbp::GuillotineBinPack (the same
// rectangle packer the rest of Fritzing already uses), so the former
// corner-stitching / cmrouter Tile pipeline - and the thousands of lines of
// batch-mode plumbing around it - have been removed. The only surviving piece
// is makeSVGs(), a per-board per-layer SVG render helper that the
// wizard-driven FabExporter (src/svg/fabexporter.cpp) reuses verbatim.

#include "panelizer.h"
#include "../debugdialog.h"
#include "../sketch/pcbsketchwidget.h"
#include "../utils/textutils.h"
#include "../utils/graphicsutils.h"
#include "../svg/gerbergenerator.h"
#include "../connectors/connectoritem.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QMultiHash>
#include <QStringList>

///////////////////////////////////////////////////////////

// Extract the text content of every <text> element in an SVG. makeSVGs() uses
// this to emit a companion .txt file alongside the rendered silk/copper layers.
static void collectTexts(const QString & svg, QStringList & strings) {
	QDomDocument doc;
	doc.setContent(svg);
	QDomElement root = doc.documentElement();
	QDomNodeList domNodeList = root.elementsByTagName("text");
	for (int i = 0; i < domNodeList.count(); i++) {
		QDomElement textElement = domNodeList.at(i).toElement();
		QString string;
		QDomNodeList childList = textElement.childNodes();
		for (int j = 0; j < childList.count(); j++) {
			QDomNode child = childList.item(j);
			if (child.isText()) {
				string.append(child.nodeValue());
			}
		}
		strings.append(string);
	}
}

void Panelizer::makeSVGs(MainWindow * mainWindow, ItemBase * board, const QString & boardName, QList<LayerThing> & layerThingList, QDir & saveDir, QFileInfo & copyInfo) {
	try {

		QString maskTop;
		QString maskBottom;
		QStringList texts;
		QMultiHash<long, ConnectorItem *> treatAsCircle;

		bool needsRedo = false;
		int missing = 0;
		foreach (LayerThing layerThing, layerThingList) {
			QString name = layerThing.name;
			QString filename = saveDir.absoluteFilePath(QString("%1_%2_%3.svg").arg(boardName).arg(board->id()).arg(name));
			QFileInfo info(filename);
			if (info.exists()) {
				if (info.lastModified() <= copyInfo.lastModified()) {
					// need to save these files again
					needsRedo = true;
					break;
				}
			}
			else {
				missing++;
			}
		}

		if (!needsRedo) {
			if (missing < layerThingList.count()) {
				// assume some number of missing files wouldn't have been written out anyway
				// but if all are missing, then they were never written in the first place
				return;
			}
		}

		foreach (LayerThing layerThing, layerThingList) {
			QString name = layerThing.name;
			QString filename = saveDir.absoluteFilePath(QString("%1_%2_%3.svg").arg(boardName).arg(board->id()).arg(name));

			SVG2gerber::ForWhy forWhy = layerThing.forWhy;
			QList<ItemBase *> copperLogoItems, holes;
			switch (forWhy) {
			case SVG2gerber::ForPasteMask:
				mainWindow->pcbView()->hideHoles(holes);
				[[fallthrough]];
			case SVG2gerber::ForMask:
				mainWindow->pcbView()->hideCopperLogoItems(copperLogoItems);
			default:
				break;
			}

			RenderThing renderThing;
			renderThing.printerScale = GraphicsUtils::SVGDPI;
			renderThing.blackOnly = true;
			renderThing.dpi = GraphicsUtils::StandardFritzingDPI;
			renderThing.hideTerminalPoints = true;
			renderThing.selectedItems = renderThing.renderBlocker = false;
			QString one = mainWindow->pcbView()->renderToSVG(renderThing, board, layerThing.layerList);

			QString clipString;
			bool wantText = false;
			switch (forWhy) {
			case SVG2gerber::ForOutline:
				one = GerberGenerator::cleanOutline(one);
				break;
			case SVG2gerber::ForPasteMask:
				mainWindow->pcbView()->restoreItemVisibility(copperLogoItems);
				mainWindow->pcbView()->restoreItemVisibility(holes);
				one = mainWindow->pcbView()->makePasteMask(one, board, GraphicsUtils::StandardFritzingDPI, layerThing.layerList);
				if (one.isEmpty()) continue;

				forWhy = SVG2gerber::ForCopper;
				break;
			case SVG2gerber::ForMask:
				mainWindow->pcbView()->restoreItemVisibility(copperLogoItems);
				one = TextUtils::expandAndFill(one, "black", GerberGenerator::MaskClearanceMils * 2);
				forWhy = SVG2gerber::ForCopper;
				if (name.contains("bottom")) {
					maskBottom = one;
				}
				else {
					maskTop = one;
				}
				break;
			case SVG2gerber::ForSilk:
				wantText = true;
				if (name.contains("bottom")) {
					clipString = maskBottom;
				}
				else {
					clipString = maskTop;
				}
				break;
			case SVG2gerber::ForCopper:
			case SVG2gerber::ForDrill:
				treatAsCircle.clear();
				foreach (QGraphicsItem * item, mainWindow->pcbView()->scene()->collidingItems(board)) {
					ConnectorItem * connectorItem = dynamic_cast<ConnectorItem *>(item);
					if (connectorItem == NULL) continue;
					if (!connectorItem->isPath()) continue;
					if (connectorItem->radius() == 0) continue;

					treatAsCircle.insert(connectorItem->attachedToID(), connectorItem);
				}
				wantText = true;
				break;
			default:
				wantText = true;
				break;
			}

			if (wantText) {
				collectTexts(one, texts);
				//DebugDialog::debug("one " + one);
			}

			QString two = GerberGenerator::clipToBoard(one, board, name, forWhy, clipString, true, treatAsCircle);
			treatAsCircle.clear();
			if (two.isEmpty()) continue;

			TextUtils::writeUtf8(filename, two);
		}

		if (texts.count() > 0) {
			QString filename = saveDir.absoluteFilePath(QString("%1_%2_%3.txt").arg(boardName).arg(board->id()).arg("texts"));
			TextUtils::writeUtf8(filename, texts.join("\n"));
		}
	}
	catch (const char * msg) {
		DebugDialog::debug(QString("panelizer error 1 %1 %2").arg(boardName).arg(msg));
	}
	catch (const QString & msg) {
		DebugDialog::debug(QString("panelizer error 2 %1 %2").arg(boardName).arg(msg));
	}
	catch (...) {
		DebugDialog::debug(QString("panelizer error 3 %1").arg(boardName));
	}
}
