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
                 QPointF textPos, const QString & textContent,
                 // Stroke width in the part's user units. Hardcoding 1 here produced a black blob on
                 // small-footprint parts (see typicalStrokeWidth): the value is relative to viewBox.
                 double strokeWidth = 0.25);

// Convenience: append a freshly built primitive to 'parent' and return it (so it becomes a child
// in document order, which is what PEDocument.build will then read).
QDomElement addTo(QDomDocument & doc, QDomElement & parent, PEElementKind kind,
                  const QString & id, const QString & cssClass, const QRectF & rect);

// P7: map a DOM element's tag name to its modelled kind (rect/line/circle/.../path/text). Returns
// Unknown for a group or unrecognized tag. Exposed so the live move/resize wiring can recover the
// element's kind from the DOM node it found by id (the same mapping PEDocument.build uses internally).
PEElementKind kindForElement(const QDomElement & element);

// Pick a stroke width that looks right on THIS part. stroke-width is expressed in the part's own
// user units, so one hardcoded value is a hairline on a 500-unit IC footprint and a black blob on a
// 15-unit connector. Take a representative width from geometry that already exists (the median, so a
// single fat outline does not set the tone); fall back to a nominal 0.25 for an empty layer.
double typicalStrokeWidth(const QDomElement & root);

// Parse an <svg> element's viewBox attribute into user units. Returns a NULL rect when absent or
// malformed, because callers clamp new geometry against the result -- a plausible-looking fallback
// would silently park shapes at the origin instead of reporting "no usable box". Reads only the four
// numbers; when width/height + preserveAspectRatio also matter, prefer QSvgRenderer::viewBoxF().
QRectF viewBoxFromAttribute(const QDomElement & svgRoot);


}   // namespace PEPrimitive

#endif   // PEPRIMITIVE_H
