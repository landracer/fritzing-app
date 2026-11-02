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

#ifndef PETRANSFORM_H
#define PETRANSFORM_H

#include <QDomElement>
#include <QRectF>
#include <QTransform>
#include <QPainterPath>

// P8 (docs/design/vector-editor.md section B.2(b)): the "flatten-and-drop the transform at load"
// contract, kept pure so it is golden-testable without any widget.
//
// The live editor previously baked each element's inherited group transform into its bounds for
// *display* only (getPixelBounds), while moveBy/editPathAnchor rewrote the element's own attrs in
// its own coordinate space -- so a graphic living under a <g transform=...> was re-transformed on
// re-render. PETransform closes that gap: it resolves the inherited transform into the element's own
// geometry (and then removes the transform so stored == displayed).
//
// The path-decode QObject adapter (PEPathShapeAdapter) is defined in petransform.cpp (file-local), NOT
// here: moc'ing a Q_OBJECT class declared in a header wired into the aggregate build trips the qmake
// path-keying hazard in AGENTS.md section 5, so the signal receiver lives in the .cpp (moc keys on it).
namespace PETransform {

// The transform chain a graphic inherits for hit-testing: every ancestor <g>'s transform, innermost
// last, plus the element's own transform attribute. Empty if no ancestor and no own transform.
QTransform inheritedTransform(const QDomElement & element);

// Bake 'transform' into the element's own geometry attributes in place:
//   rect / image  -> map its box (x,y,w,h) via mapRect
//   line          -> map both endpoints
//   circle        -> map cx,cy; keep r only under a pure translate, else drop r (ellipse not storeable as r)
//   ellipse       -> map center; rx/ry scaled per-axis
//   polyline / polygon -> map every point pair
//   path          -> decode 'd' and re-encode every coordinate through the transform; every segment is
//                    emitted in absolute form (command letters kept, abs/rel normalized to absolute)
//   text          -> map x,y
// A group or an unrecognized tag returns false and is left untouched.
bool bakeInto(QDomElement & element, const QTransform & transform);

// Parse + validate an SVG 'transform' attribute string into a QTransform. Returns false if the string
// is empty or malformed (the caller then leaves the element alone -- no silent rewrite).
bool parseTransform(const QString & transformString, QTransform & out);

// Flatten the transform 'element' inherits from its ancestors (and its own 'transform' attribute) into
// the element's geometry, then remove the element's own 'transform' attribute. No-op (returns false) if
// the inherited chain is identity/absent or the element kind is untransformable. This is the exact
// load-time operation P8's typed build performs so a later moveBy/editPathAnchor needs no re-apply.
bool flatten(QDomElement & element);

// P10 (docs/design/vector-editor.md F.5): the live item's self-render. Build the element's shape in the
// caller's local coordinates (the element's own absolute attrs, from flatten()): rect/image -> rect,
// circle/ellipse -> ellipse, line -> line, polyline/polygon -> open move+line-to over the vertex list (a
// polygon is NOT closed -- Qt 6 QPainterPath has no close-segment type), path -> decoded path segments,
// text -> its anchor point (zero-area). Returns false for an unmodelled/unknown tag so the caller can fall
// back (empty path). Pure + golden-testable (no widget).
bool shapeFromElement(const QDomElement & element, QPainterPath & out);

}   // namespace PETransform


#endif   // PETRANSFORM_H
