/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

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

#ifndef PEPRIMITIVEGEOM_H
#define PEPRIMITIVEGEOM_H

#include <QString>
#include <QRectF>
#include <QPointF>
#include <QDomElement>

// Minimal, self-contained per-kind SVG geometry helpers for the sole-renderer editor
// (docs/design/pe-sole-renderer.md). Ported -- not linked -- from the tested PEElement::moveBy /
// resizeRect in src/partseditor/pedocument.cpp, so the standalone editor stays Qt-Core/Gui/Xml only
// (no svgpath trio, no petransform file-local-moc). The behaviour is intentionally identical:
// translate a graphic by rewriting its OWN geometry attributes in place, preserving each kind's
// coordinate convention. A <path> is left untouched here (it needs PEPath decoding); movePath is a
// deliberate no-op for now, so a path element is selected but not draggable until the path engine is
// wired in -- an honest gap, not a silent corruption.
namespace PEPrimGeom {

enum class Kind { Unknown, Group, Rect, Circle, Ellipse, Line, Polyline, Polygon, Path, Text };

Kind kindFor(const QDomElement & element);
QString kindName(Kind k);

// Translate the element in place by delta (its own user units). Returns true if it rewrote geometry.
bool moveBy(QDomElement & element, QPointF delta);

// Replace a <rect>'s box outright. False for non-rect kinds (they store a position, not a size).
bool resizeRect(QDomElement & element, const QRectF & to);

// The bounding box of the element in its OWN local user units (from geometry attrs, not the
// renderer), used to draw the selection outline and drive resize. Empty when not computable.
QRectF localBounds(const QDomElement & element);

// Find the first element (depth-first) whose id == id. Null element if absent.
QDomElement findById(const QDomElement & root, const QString & id);

} // namespace PEPrimGeom

#endif // PEPRIMITIVEGEOM_H
