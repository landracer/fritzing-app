// PESvgEditor geometry helpers (docs/design/pe-sole-renderer.md).
// SPDX-License-Identifier: GPL-3.0-or-later   Copyright (c) 2026 Fritzing
//
// Per-kind SVG geometry ops, ported (not linked) from the tested PEElement::moveBy / resizeRect in
// src/partseditor/pedocument.cpp, so the standalone editor stays Qt-Core/Gui/Xml only.

#include "peprimitivegeom.h"

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

bool resizeRect(QDomElement & element, const QRectF & to)
{
    if (kindFor(element) != Kind::Rect) return false;
    element.setAttribute("x", QString::number(to.x()));
    element.setAttribute("y", QString::number(to.y()));
    element.setAttribute("width", QString::number(to.width()));
    element.setAttribute("height", QString::number(to.height()));
    return true;
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
            QRectF r;
            bool any = false;
            for (const QString & pair : pairs) {
                const QStringList xy = pair.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (xy.count() != 2) return QRectF();
                bool ok = false;
                double x = xy.at(0).toDouble(&ok); if (!ok) return QRectF();
                double y = xy.at(1).toDouble(&ok); if (!ok) return QRectF();
                if (!any) { r = QRectF(x, y, 0, 0); any = true; }
                else r |= QRectF(x, y, 0, 0);
            }
            return r;
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

} // namespace PEPrimGeom
