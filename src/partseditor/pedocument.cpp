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

#include "pedocument.h"

#include <QDomDocument>
#include <cstring>   // strlen for the "connector" prefix length

#include "pepath.h"

// Map an SVG tag name to a modelled kind.
static PEElementKind kindForTag(const QString & tag) {
    if (tag == QLatin1String("g"))        return PEElementKind::Group;
    if (tag == QLatin1String("rect"))      return PEElementKind::Rect;
    if (tag == QLatin1String("circle"))    return PEElementKind::Circle;
    if (tag == QLatin1String("ellipse"))   return PEElementKind::Ellipse;
    if (tag == QLatin1String("line"))      return PEElementKind::Line;
    if (tag == QLatin1String("polyline"))  return PEElementKind::Polyline;
    if (tag == QLatin1String("polygon"))   return PEElementKind::Polygon;
    if (tag == QLatin1String("path"))      return PEElementKind::Path;
    if (tag == QLatin1String("text"))      return PEElementKind::Text;
    return PEElementKind::Unknown;
}

bool PEElement::isConnectorPin() const {
    return id.startsWith(QLatin1String("connector")) && id.endsWith(QLatin1String("pin"));
}

bool PEElement::isConnectorTerminal() const {
    return id.startsWith(QLatin1String("connector")) && id.endsWith(QLatin1String("terminal"));
}

int PEElement::connectorIndex() const {
    // "connectorNpin" / "connectorNterminal" -> N. Returns -1 if not a connector node.
    if (!id.startsWith(QLatin1String("connector"))) return -1;
    QString rest = id.mid(static_cast<int>(strlen("connector")));
    int pinPos = rest.indexOf(QLatin1String("pin"));
    int termPos = rest.indexOf(QLatin1String("terminal"));
    int cut = -1;
    if (pinPos >= 0 && termPos >= 0)      cut = qMin(pinPos, termPos);
    else if (pinPos >= 0)                 cut = pinPos;
    else if (termPos >= 0)                cut = termPos;
    if (cut < 0)                          return -1;
    bool ok = false;
    int n = rest.left(cut).toInt(&ok);
    return ok ? n : -1;
}

QString PEElement::editPathAnchor(int segmentIndex, QPointF anchor) const {
    if (kind != PEElementKind::Path) return QString();
    const QString d = element.attribute(QStringLiteral("d"));
    if (d.isEmpty()) return QString();

    PEPath path = PEPath::fromD(d);
    if (!path.ok()) return QString();

    // Rebuild the segment list, recomputing the running cursor so we know the absolute
    // position that segment's anchor encodes, then encode 'anchor' in the same convention.
    QVector<PEPathSegment> segs = path.segments();
    if (segmentIndex < 0 || segmentIndex >= segs.count()) return QString();

    QVector<PEPathSegment> out;
    QPointF cursor(0, 0);
    for (int i = 0; i < segs.count(); ++i) {
        const PEPathSegment & s = segs.at(i);
        PEPathSegment ns = s;
        if (i == segmentIndex) {
            if (s.relative) ns.args = { anchor.x() - cursor.x(), anchor.y() - cursor.y() };
            else            ns.args = { anchor.x(), anchor.y() };
        }
        out.append(ns);
        cursor = s.anchor(cursor);
    }
    PEPath edited = PEPath::fromSegments(out);
    return edited.serialize();
}

void PEDocument::build(const QDomDocument & doc) {
    m_elements.clear();
    m_dom = const_cast<QDomDocument *>(&doc);
    QDomElement rootElement = doc.documentElement();
    if (rootElement.isNull()) {
        return;
    }

    // Depth-first, document order -- mirrors the live initSvgTree traversal: take the
    // first element, then push its children to the front so children precede siblings.
    QList<QDomElement> traverse;
    traverse << rootElement;
    while (!traverse.isEmpty()) {
        QDomElement element = traverse.takeFirst();

        QList<QDomElement> next;
        QDomElement child = element.firstChildElement();
        while (!child.isNull()) {
            next << child;
            child = child.nextSiblingElement();
        }
        while (!next.isEmpty()) {
            traverse.push_front(next.takeLast());
        }

        PEElement pe;
        pe.kind = kindForTag(element.tagName());
        pe.id = element.attribute(QStringLiteral("id"));
        pe.cssClass = element.attribute(QStringLiteral("class"));
        // For a rect we can read geometry purely from attributes (no renderer).
        if (pe.kind == PEElementKind::Rect) {
            bool ok = false;
            double x = element.attribute(QStringLiteral("x")).toDouble(&ok);
            double y = element.attribute(QStringLiteral("y")).toDouble(&ok);
            double w = element.attribute(QStringLiteral("width")).toDouble(&ok);
            double h = element.attribute(QStringLiteral("height")).toDouble(&ok);
            if (ok) {
                pe.bounds = QRectF(x, y, w, h);
                pe.hasBounds = true;
            }
        }
        pe.element = element;
        m_elements.append(pe);
    }
}

QString PEDocument::toXml() const {
    if (m_dom == nullptr) return QString();
    return m_dom->toString();
}

PEElement PEDocument::findById(const QString & id) const {
    for (const PEElement & pe : m_elements) {
        if (pe.id == id) return pe;
    }
    return PEElement();   // Unknown / null element
}

QList<PEElement> PEDocument::byKind(PEElementKind kind) const {
    QList<PEElement> out;
    for (const PEElement & pe : m_elements) {
        if (pe.kind == kind) out.append(pe);
    }
    return out;
}

QList<PEElement> PEDocument::connectorPins() const {
    QList<PEElement> out;
    for (const PEElement & pe : m_elements) {
        if (pe.isConnectorPin()) out.append(pe);
    }
    return out;
}
