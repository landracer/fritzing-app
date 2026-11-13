// PESvgEditor geometry helpers (docs/design/pe-sole-renderer.md).
// SPDX-License-Identifier: GPL-3.0-or-later   Copyright (c) 2026 Fritzing
//
// Per-kind SVG geometry ops, ported (not linked) from the tested PEElement::moveBy / resizeRect in
// src/partseditor/pedocument.cpp, so the standalone editor stays Qt-Core/Gui/Xml only.

#include "peprimitivegeom.h"

#include "pepath.h"

#include <QStringList>
#include <QLatin1Char>

namespace PEPrimGeom {

// Mirrors PEDocument's tag->kind mapping (pedocument.cpp) so the two models agree.
Kind kindFor(const QDomElement & element)
{
    const QString t = element.tagName().toLower();
    if (t == QLatin1String("rect"))     return Kind::Rect;
    if (t == QLatin1String("circle"))   return Kind::Circle;
    if (t == QLatin1String("ellipse"))  return Kind::Ellipse;
    if (t == QLatin1String("line"))     return Kind::Line;
    if (t == QLatin1String("polyline")) return Kind::Polyline;
    if (t == QLatin1String("polygon"))  return Kind::Polygon;
    if (t == QLatin1String("path"))     return Kind::Path;
    if (t == QLatin1String("text"))     return Kind::Text;
    if (t == QLatin1String("g"))        return Kind::Group;
    return Kind::Unknown;
}

QString kindName(Kind k)
{
    switch (k) {
        case Kind::Rect: return QStringLiteral("rect");
        case Kind::Circle: return QStringLiteral("circle");
        case Kind::Ellipse: return QStringLiteral("ellipse");
        case Kind::Line: return QStringLiteral("line");
        case Kind::Polyline: return QStringLiteral("polyline");
        case Kind::Polygon: return QStringLiteral("polygon");
        case Kind::Path: return QStringLiteral("path");
        case Kind::Text: return QStringLiteral("text");
        case Kind::Group: return QStringLiteral("group");
        default: return QStringLiteral("unknown");
    }
}

// Same per-kind attribute rewrite as PEElement::moveBy (pedocument.cpp:112). A <path> would need
// PEPath decode/re-encode (deliberately deferred); return false so it is not silently corrupted.
bool moveBy(QDomElement & element, QPointF delta)
{
    switch (kindFor(element)) {
        case Kind::Rect: {
            bool ok = false;
            double x = element.attribute("x").toDouble(&ok); if (!ok) return false;
            double y = element.attribute("y").toDouble(&ok); if (!ok) return false;
            element.setAttribute("x", QString::number(x + delta.x()));
            element.setAttribute("y", QString::number(y + delta.y()));
            return true;
        }
        case Kind::Line: {
            bool ok = false;
            double x1 = element.attribute("x1").toDouble(&ok); if (!ok) return false;
            double y1 = element.attribute("y1").toDouble(&ok); if (!ok) return false;
            double x2 = element.attribute("x2").toDouble(&ok); if (!ok) return false;
            double y2 = element.attribute("y2").toDouble(&ok); if (!ok) return false;
            element.setAttribute("x1", QString::number(x1 + delta.x()));
            element.setAttribute("y1", QString::number(y1 + delta.y()));
            element.setAttribute("x2", QString::number(x2 + delta.x()));
            element.setAttribute("y2", QString::number(y2 + delta.y()));
            return true;
        }
        case Kind::Circle:
        case Kind::Ellipse: {
            bool ok = false;
            double cx = element.attribute("cx").toDouble(&ok); if (!ok) return false;
            double cy = element.attribute("cy").toDouble(&ok); if (!ok) return false;
            element.setAttribute("cx", QString::number(cx + delta.x()));
            element.setAttribute("cy", QString::number(cy + delta.y()));
            return true;
        }
        case Kind::Text: {
            bool ok = false;
            double x = element.attribute("x").toDouble(&ok); if (!ok) return false;
            double y = element.attribute("y").toDouble(&ok); if (!ok) return false;
            element.setAttribute("x", QString::number(x + delta.x()));
            element.setAttribute("y", QString::number(y + delta.y()));
            return true;
        }
        case Kind::Polyline:
        case Kind::Polygon: {
            const QString pts = element.attribute("points");
            if (pts.isEmpty()) return false;
            const QStringList pairs = pts.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QStringList out;
            for (const QString & pair : pairs) {
                const QStringList xy = pair.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (xy.count() != 2) return false;
                bool ok = false;
                double x = xy.at(0).toDouble(&ok); if (!ok) return false;
                double y = xy.at(1).toDouble(&ok); if (!ok) return false;
                out << QStringLiteral("%1,%2").arg(QString::number(x + delta.x()),
                                                   QString::number(y + delta.y()));
            }
            if (out.isEmpty()) return false;
            element.setAttribute("points", out.join(QLatin1Char(' ')));
            return true;
        }
        case Kind::Path:
        default:
            return false;   // path needs the path engine; group/unknown are not geometric
    }
}

// R1: kind-generic resize. Fit the element's geometry to the box `to`. This used to be rect-only and return
// false for everything else, so a resize grip on a circle/line/ellipse silently did nothing even when the
// grips drew and the command ran. Each case maps the box onto that kind's own attrs. `from` is the element's
// current bounds (filled in by the caller from localBounds) and is what the point-list kinds scale from.
// A <path> stays honestly unsupported -- it is edited per-anchor via editPathAnchor, so this returns false.
bool resizeTo(QDomElement & element, const QRectF & from, const QRectF & to)
{
    switch (kindFor(element)) {
        case Kind::Rect:
            element.setAttribute("x", QString::number(to.x()));
            element.setAttribute("y", QString::number(to.y()));
            element.setAttribute("width", QString::number(to.width()));
            element.setAttribute("height", QString::number(to.height()));
            return true;

        case Kind::Line:
            // A line's box IS its two endpoints, so the box corners map directly.
            element.setAttribute("x1", QString::number(to.left()));
            element.setAttribute("y1", QString::number(to.top()));
            element.setAttribute("x2", QString::number(to.right()));
            element.setAttribute("y2", QString::number(to.bottom()));
            return true;

        case Kind::Ellipse: {
            if (to.width() <= 0 || to.height() <= 0) return false;
            element.setAttribute("cx", QString::number(to.center().x()));
            element.setAttribute("cy", QString::number(to.center().y()));
            element.setAttribute("rx", QString::number(to.width() / 2.0));
            element.setAttribute("ry", QString::number(to.height() / 2.0));
            return true;
        }
        case Kind::Polyline:
        case Kind::Polygon: {
            // Scale the point list from the old box to the new one.
            if (!from.isValid() || from.width() <= 0 || from.height() <= 0) return false;
            const double sx = to.width()  / from.width();
            const double sy = to.height() / from.height();
            const QStringList pairs = element.attribute("points").simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QStringList out;
            for (const QString & pair : pairs) {
                const QStringList xy = pair.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (xy.count() != 2) return false;
                bool okX = false, okY = false;
                const double x = xy.at(0).toDouble(&okX);
                const double y = xy.at(1).toDouble(&okY);
                if (!okX || !okY) return false;
                out << QStringLiteral("%1,%2").arg(QString::number(to.left() + (x - from.left()) * sx),
                                                   QString::number(to.top()  + (y - from.top())  * sy));
            }
            if (out.isEmpty()) return false;
            element.setAttribute("points", out.join(QLatin1Char(' ')));
            return true;
        }
        case Kind::Circle:
        case Kind::Path:
        case Kind::Group:
        case Kind::Unknown:
        case Kind::Text:
        default:
            return false;   // circle is round (no per-axis box resize); path is node-edited; others aren't geometric
    }
}

// True when `element`'s kind supports resizeTo. A <path> is NOT box-resizable (it is edited per-anchor via
// editPathAnchor), and Group/Unknown never are. Drives whether the editor draws resize grips.
bool isResizable(const QDomElement & element)
{
    if (element.isNull()) return false;
    switch (kindFor(element)) {
        case Kind::Rect:
        case Kind::Ellipse:
        case Kind::Line:
        case Kind::Polyline:
        case Kind::Polygon:
            return true;
        default:
            return false;   // circle is round (no honest box resize); path is node-edited; others aren't geometric
    }
}


QRectF localBounds(const QDomElement & element)
{
    switch (kindFor(element)) {
        case Kind::Rect: {
            bool ok = false;
            double x = element.attribute("x").toDouble(&ok); if (!ok) return QRectF();
            double y = element.attribute("y").toDouble(&ok); if (!ok) return QRectF();
            double w = element.attribute("width").toDouble(&ok); if (!ok) return QRectF();
            double h = element.attribute("height").toDouble(&ok); if (!ok) return QRectF();
            return QRectF(x, y, w, h);
        }
        case Kind::Circle: {
            bool ok = false;
            double cx = element.attribute("cx").toDouble(&ok); if (!ok) return QRectF();
            double cy = element.attribute("cy").toDouble(&ok); if (!ok) return QRectF();
            double r  = element.attribute("r").toDouble(&ok);  if (!ok) return QRectF();
            return QRectF(cx - r, cy - r, 2 * r, 2 * r);
        }
        case Kind::Ellipse: {
            bool ok = false;
            double cx = element.attribute("cx").toDouble(&ok); if (!ok) return QRectF();
            double cy = element.attribute("cy").toDouble(&ok); if (!ok) return QRectF();
            double rx = element.attribute("rx").toDouble(&ok); if (!ok) return QRectF();
            double ry = element.attribute("ry").toDouble(&ok); if (!ok) return QRectF();
            return QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry);
        }
        case Kind::Line: {
            bool ok = false;
            double x1 = element.attribute("x1").toDouble(&ok); if (!ok) return QRectF();
            double y1 = element.attribute("y1").toDouble(&ok); if (!ok) return QRectF();
            double x2 = element.attribute("x2").toDouble(&ok); if (!ok) return QRectF();
            double y2 = element.attribute("y2").toDouble(&ok); if (!ok) return QRectF();
            return QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
        }
        case Kind::Polyline:
        case Kind::Polygon: {
            const QString pts = element.attribute("points");
            if (pts.isEmpty()) return QRectF();
            const QStringList pairs = pts.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            // Bounding box = min/max over all points. (Do NOT build it by uniting per-point two-arg rects:
            // QRectF(tl,br) with equal points is a zero-size rect and united() of those stays degenerate.)
            double minX = 0, minY = 0, maxX = 0, maxY = 0;
            bool any = false;
            for (const QString & pair : pairs) {
                const QStringList xy = pair.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (xy.count() != 2) return QRectF();
                bool ok = false;
                double x = xy.at(0).toDouble(&ok); if (!ok) return QRectF();
                double y = xy.at(1).toDouble(&ok); if (!ok) return QRectF();
                if (!any) { minX = maxX = x; minY = maxY = y; any = true; }
                else { minX = qMin(minX, x); maxX = qMax(maxX, x); minY = qMin(minY, y); maxY = qMax(maxY, y); }
            }
            if (!any) return QRectF();
            return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));   // already ordered: tl then br
        }
        default:
            return QRectF();   // text/path bounds need font metrics / path decode; deferred
    }
}

QDomElement findById(const QDomElement & root, const QString & id)
{
    QDomElement e = root.firstChildElement();
    while (!e.isNull()) {
        if (e.attribute("id") == id) return e;
        QDomElement found = findById(e, id);
        if (!found.isNull()) return found;
        e = e.nextSiblingElement();
    }
    return QDomElement();
}

// ---- Path node editing -------------------------------------------------------------------------------
// Ported from the tested PEElement::pathAnchors / editPathAnchor (pedocument.cpp). The element stores its
// own 'd'; anchors are read/edited in the element's own user-unit space (the ancestor transform was already
// baked in by PETransform::flatten at load, so these coords already agree with the scene).

QVector<QPointF> pathAnchors(const QDomElement & element)
{
    if (kindFor(element) != Kind::Path) return QVector<QPointF>();
    const QString d = element.attribute(QStringLiteral("d"));
    if (d.isEmpty()) return QVector<QPointF>();
    PEPath path = PEPath::fromD(d);
    if (!path.ok()) return QVector<QPointF>();
    return path.anchors();
}

bool editPathAnchor(QDomElement & element, int index, QPointF anchor)
{
    if (kindFor(element) != Kind::Path) return false;
    const QString d = element.attribute(QStringLiteral("d"));
    if (d.isEmpty()) return false;
    PEPath path = PEPath::fromD(d);
    if (!path.ok()) return false;

    const QVector<PEPathSegment> segs = path.segments();
    if (index < 0 || index >= segs.count()) return false;

    // Rebuild the segment list, recomputing the running cursor so we know the absolute position that
    // segment's anchor encodes, then encode 'anchor' in the same (absolute/relative) convention.
    QVector<PEPathSegment> out;
    QPointF cursor(0, 0);
    for (int i = 0; i < segs.count(); ++i) {
        const PEPathSegment & s = segs.at(i);
        PEPathSegment ns = s;
        if (i == index) {
            if (s.relative) ns.args = { anchor.x() - cursor.x(), anchor.y() - cursor.y() };
            else            ns.args = { anchor.x(), anchor.y() };
        }
        out.append(ns);
        cursor = s.anchor(cursor);
    }
    PEPath edited = PEPath::fromSegments(out);
    const QString newD = edited.serialize();
    if (newD.isEmpty()) return false;
    element.setAttribute(QStringLiteral("d"), newD);
    return true;
}

} // namespace PEPrimGeom
