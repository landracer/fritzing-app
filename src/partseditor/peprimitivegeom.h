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
#include <QVector>
#include <QDomElement>

// Minimal, self-contained per-kind SVG geometry helpers for the sole-renderer editor
// (docs/design/pe-sole-renderer.md). They rewrite a graphic's OWN geometry attributes in place, preserving each
// kind's coordinate convention: moveBy translates, resizeTo fits a kind to a box. A <path> is moved/edited only
// through the path node-editing API (pathAnchors/editPathAnchor below), which decodes its 'd'; those rely on
// PEPath/petransform decoding, so the standalone editor does link that path-decode stack.
namespace PEPrimGeom {

enum class Kind { Unknown, Group, Rect, Circle, Ellipse, Line, Polyline, Polygon, Path, Text };

Kind kindFor(const QDomElement & element);
QString kindName(Kind k);

// Translate the element in place by delta (its own user units). Returns true if it rewrote geometry.
bool moveBy(QDomElement & element, QPointF delta);

// R1: kind-generic resize. Fit the element's geometry to the box `to`, per kind (rect -> x/y/w/h; line ->
// endpoints; ellipse -> box extents; polyline/polygon -> scale the point list from `from` to `to`). `from` is
// the element's current bounds (the caller fills it from localBounds) and is what the point-list kinds scale
// from. A <circle> is round (single r) and a <path> is node-edited, so both are honestly unsupported here, as
// are Group/Unknown/Text -- those return false so the caller never writes an unchanged file and claims an edit.
bool resizeTo(QDomElement & element, const QRectF & from, const QRectF & to);

// True when `element`'s kind supports resizeTo (rect/ellipse/line/polyline/polygon). A <path> is not
// box-resizable (it is edited node by node), and a <circle> has no honest per-axis box resize. Drives
// whether the editor draws resize grips.
bool isResizable(const QDomElement & element);

// The bounding box of the element in its OWN local user units (from geometry attrs, not the
// renderer), used to draw the selection outline and drive resize. Empty when not computable.
QRectF localBounds(const QDomElement & element);

// Find the first element (depth-first) whose id == id. Null element if absent.
QDomElement findById(const QDomElement & root, const QString & id);

// ---- Path node editing -------------------------------------------------------------------------------
// The element stores its own 'd'; anchors are read/edited in the element's own user-unit space (the ancestor
// transform was already baked in by PETransform::flatten at load, so these coords agree with the scene).

// The end-anchor of every decoded segment of a <path>, in the element's own user units, in segment order --
// the points a node-editing grip drags. Empty for a non-path or an undecodable 'd'.
QVector<QPointF> pathAnchors(const QDomElement & element);

// Replace the anchor of segment 'index' with 'anchor' (user units) and write the re-encoded 'd' back in place,
// preserving the source's own absolute/relative convention (a relative arg is target-cursor). False for a
// non-path, an out-of-range index, or an undecodable 'd'.
bool editPathAnchor(QDomElement & element, int index, QPointF anchor);

} // namespace PEPrimGeom

#endif // PEPRIMITIVEGEOM_H
