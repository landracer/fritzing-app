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

#ifndef PEPRIMITIVE_H
#define PEPRIMITIVE_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QDomElement>
#include <QDomDocument>

#include "pedocument.h"   // PEElementKind

// P3 (docs/design/vector-editor.md section F): primitive creation. The live parts editor has
// no element-creation path at all (the old editor's addRectToSvg etc. are parked in obsolete/),
// so this is the greenfield typed builder that the future tool-driven creation will drive off.
//
// Like the rest of the vector-editor model this is pure/inert: given a kind + geometry (+ an
// id/class), build the matching Fritzing-convention DOM element. The live tool wiring (mouse
// draw -> these builders -> insert into the view's QDomDocument) is a later layer.

namespace PEPrimitive {

// Build the DOM element for a primitive. The geometry argument is overloaded per kind:
//   Rect      -> rect (x,y,w,h)
//   Line      -> line from lineStart to lineEnd
//   Circle    -> circle centered at circleCenter with circleRadius
//   Ellipse   -> ellipse centered at ellipseCenter with radii ellipseRx/ellipseRy
//   Polyline  -> polyline of points[]  (Polygon -> polygon of points[])
//   Path      -> path whose 'd' is pathData
//   Text      -> text at textPos containing textContent
// 'id'/'cssClass' are applied when non-empty (the live convention gives created graphics an id).
QDomElement make(QDomDocument & doc, PEElementKind kind, const QString & id, const QString & cssClass,
                 const QRectF & rect, QPointF lineStart, QPointF lineEnd,
                 QPointF circleCenter, double circleRadius,
                 QPointF ellipseCenter, double ellipseRx, double ellipseRy,
                 const QList<QPointF> & points, const QString & pathData,
                 QPointF textPos, const QString & textContent);

// Convenience: append a freshly built primitive to 'parent' and return it (so it becomes a child
// in document order, which is what PEDocument.build will then read).
QDomElement addTo(QDomDocument & doc, QDomElement & parent, PEElementKind kind,
                  const QString & id, const QString & cssClass, const QRectF & rect);

}   // namespace PEPrimitive

#endif   // PEPRIMITIVE_H
