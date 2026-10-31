/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the GNU General Public License as published under the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#ifndef PEBLANKPART_H
#define PEBLANKPART_H

#include <QString>
#include <QDomDocument>
#include <QDomElement>
#include <QRectF>

// P6 (docs/design/vector-editor.md section F): the "create from blank" half. The live parts editor
// can only ever edit a part that already exists (setInitialItem -> loadFzp of an existing fzp); there
// is no from-nothing path. This is the pure builder for one: a minimal-but-valid Fritzing part -- a
// fzp <module> carrying the metadata a saveable part needs (version/date/label/author/title/tags/
// properties + the four <view>s' <layers image>), and a blank per-view <svg> whose root <g id=viewName>
// is the layer group the editor later draws into.
//
// Like PEPrimitive/PEConnector this is inert and DOM-only: it does not touch ViewLayer/QGraphicsItem.
// 'viewName' is the fzp <view> xml key ('breadboardView'/'schematicView'/..) and 'layerName' the
// <layer layerId>/graphic-group id ('breadboard'/'schematic'/..); the caller passes the ViewLayer
// xml names so this stays decoupled from the heavy ViewLayer/Qt-object machinery.
namespace PEBlankPart {

// A fresh module id of the live convention's shape (a random hex-ish token).
QString newModuleID();

// Build a blank part document: <module fritzingVersion moduleId> ... <views> with one
// <view><layers image=...><layer layerId=layerName/></layers></view> per known view, plus an empty
// <connectors/>. The 'image' paths are filled with placeholder "<layerName>/<id>.svg" so the fzp is
// well-formed; the caller replaces them with real saved paths when it writes the svg. Returns the root.
QDomElement buildModule(QDomDocument & doc, const QString & moduleID, const QString & title);

// A blank per-view SVG document string: the standard Fritzing svg header (viewBox in user units derived
// from 'box') with a single empty <g id=layerName> graphic group and no child graphics yet. This is the
// file that gets written when a blank part is created, and the group the editor draws into.
QString blankViewSvg(const QString & layerName, const QRectF & box);

}   // namespace PEBlankPart

#endif   // PEBLANKPART_H
