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

#include "peprimitive.h"

#include <QStringList>

namespace {
QString fmtNum(double d) { return QString::number(d); }
}   // namespace

QDomElement PEPrimitive::make(QDomDocument & doc, PEElementKind kind, const QString & id, const QString & cssClass,
                              const QRectF & rect, QPointF lineStart, QPointF lineEnd,
                              QPointF circleCenter, double circleRadius,
                              QPointF ellipseCenter, double ellipseRx, double ellipseRy,
                              const QList<QPointF> & points, const QString & pathData,
                              QPointF textPos, const QString & textContent)
{
    QDomElement el;
    switch (kind) {
    case PEElementKind::Rect:
        el = doc.createElement(QStringLiteral("rect"));
        el.setAttribute(QStringLiteral("x"), fmtNum(rect.x()));
        el.setAttribute(QStringLiteral("y"), fmtNum(rect.y()));
        el.setAttribute(QStringLiteral("width"), fmtNum(rect.width()));
        el.setAttribute(QStringLiteral("height"), fmtNum(rect.height()));
        el.setAttribute(QStringLiteral("fill"), QStringLiteral("none"));
        break;
    case PEElementKind::Line:
        el = doc.createElement(QStringLiteral("line"));
        el.setAttribute(QStringLiteral("x1"), fmtNum(lineStart.x()));
        el.setAttribute(QStringLiteral("y1"), fmtNum(lineStart.y()));
        el.setAttribute(QStringLiteral("x2"), fmtNum(lineEnd.x()));
        el.setAttribute(QStringLiteral("y2"), fmtNum(lineEnd.y()));
        el.setAttribute(QStringLiteral("fill"), QStringLiteral("none"));
        break;
    case PEElementKind::Circle:
        el = doc.createElement(QStringLiteral("circle"));
        el.setAttribute(QStringLiteral("cx"), fmtNum(circleCenter.x()));
        el.setAttribute(QStringLiteral("cy"), fmtNum(circleCenter.y()));
        el.setAttribute(QStringLiteral("r"), fmtNum(circleRadius));
        break;
    case PEElementKind::Ellipse:
        el = doc.createElement(QStringLiteral("ellipse"));
        el.setAttribute(QStringLiteral("cx"), fmtNum(ellipseCenter.x()));
        el.setAttribute(QStringLiteral("cy"), fmtNum(ellipseCenter.y()));
        el.setAttribute(QStringLiteral("rx"), fmtNum(ellipseRx));
        el.setAttribute(QStringLiteral("ry"), fmtNum(ellipseRy));
        break;
    case PEElementKind::Polyline:
    case PEElementKind::Polygon: {
        el = doc.createElement(kind == PEElementKind::Polyline ? QStringLiteral("polyline") : QStringLiteral("polygon"));
        QStringList pairs;
        for (const QPointF & p : points) {
            pairs << QStringLiteral("%1,%2").arg(fmtNum(p.x()), fmtNum(p.y()));
        }
        el.setAttribute(QStringLiteral("points"), pairs.join(QStringLiteral(" ")));
        break;
    }
    case PEElementKind::Path:
        el = doc.createElement(QStringLiteral("path"));
        el.setAttribute(QStringLiteral("d"), pathData);
        break;
    case PEElementKind::Text:
        el = doc.createElement(QStringLiteral("text"));
        el.setAttribute(QStringLiteral("x"), fmtNum(textPos.x()));
        el.setAttribute(QStringLiteral("y"), fmtNum(textPos.y()));
        el.appendChild(doc.createTextNode(textContent));
        break;
    case PEElementKind::Group:
    case PEElementKind::Unknown:
    default:
        return QDomElement();   // not a creatable primitive
    }

    if (!id.isNull() && !id.isEmpty())        el.setAttribute(QStringLiteral("id"), id);
    if (!cssClass.isNull() && !cssClass.isEmpty()) el.setAttribute(QStringLiteral("class"), cssClass);
    return el;
}

QDomElement PEPrimitive::addTo(QDomDocument & doc, QDomElement & parent, PEElementKind kind,
                               const QString & id, const QString & cssClass, const QRectF & rect)
{
    QDomElement el = make(doc, kind, id, cssClass, rect, QPointF(), QPointF(), QPointF(), 0.0,
                          QPointF(), 0.0, 0.0, QList<QPointF>(), QString(), QPointF(), QString());
    if (el.isNull()) return el;
    parent.appendChild(el);
    return el;
}
