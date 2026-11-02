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
#include "petransform.h"

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

QList<QPointF> PEElement::pathAnchors() const {
    if (kind != PEElementKind::Path) return QList<QPointF>();
    const QString d = element.attribute(QStringLiteral("d"));
    if (d.isEmpty()) return QList<QPointF>();
    PEPath path = PEPath::fromD(d);
    if (!path.ok()) return QList<QPointF>();
    return path.anchors().toList();
}

// P7: translate a graphic by rewriting its geometry attributes. Absolute-coordinate kinds
// (rect/line/circle/ellipse/text) shift their attributes; polyline/polygon shift each point pair;
// a <path> decodes 'd' and, walking with a running cursor, shifts each segment's anchor in the
// source's own abs/rel encoding (a relative segment's arg is target-cursor, and shifting both by
// the same delta shifts the target by exactly the delta, so relative encoding is preserved). A
// group/unknown is not geometric and returns false.
bool PEElement::moveBy(QPointF delta) {
    switch (kind) {
    case PEElementKind::Rect: {
        bool ok = false;
        double x = element.attribute(QStringLiteral("x")).toDouble(&ok);
        if (!ok) return false;
        double y = element.attribute(QStringLiteral("y")).toDouble(&ok);
        if (!ok) return false;
        element.setAttribute(QStringLiteral("x"), QString::number(x + delta.x()));
        element.setAttribute(QStringLiteral("y"), QString::number(y + delta.y()));
        return true;
    }
    case PEElementKind::Line: {
        bool ok = false;
        double x1 = element.attribute(QStringLiteral("x1")).toDouble(&ok); if (!ok) return false;
        double y1 = element.attribute(QStringLiteral("y1")).toDouble(&ok); if (!ok) return false;
        double x2 = element.attribute(QStringLiteral("x2")).toDouble(&ok); if (!ok) return false;
        double y2 = element.attribute(QStringLiteral("y2")).toDouble(&ok); if (!ok) return false;
        element.setAttribute(QStringLiteral("x1"), QString::number(x1 + delta.x()));
        element.setAttribute(QStringLiteral("y1"), QString::number(y1 + delta.y()));
        element.setAttribute(QStringLiteral("x2"), QString::number(x2 + delta.x()));
        element.setAttribute(QStringLiteral("y2"), QString::number(y2 + delta.y()));
        return true;
    }
    case PEElementKind::Circle:
    case PEElementKind::Ellipse: {
        bool ok = false;
        double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok); if (!ok) return false;
        double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok); if (!ok) return false;
        element.setAttribute(QStringLiteral("cx"), QString::number(cx + delta.x()));
        element.setAttribute(QStringLiteral("cy"), QString::number(cy + delta.y()));
        return true;
    }
    case PEElementKind::Text: {
        bool ok = false;
        double x = element.attribute(QStringLiteral("x")).toDouble(&ok); if (!ok) return false;
        double y = element.attribute(QStringLiteral("y")).toDouble(&ok); if (!ok) return false;
        element.setAttribute(QStringLiteral("x"), QString::number(x + delta.x()));
        element.setAttribute(QStringLiteral("y"), QString::number(y + delta.y()));
        return true;
    }
    case PEElementKind::Polyline:
    case PEElementKind::Polygon: {
        const QString pts = element.attribute(QStringLiteral("points"));
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
        element.setAttribute(QStringLiteral("points"), out.join(QStringLiteral(" ")));
        return true;
    }
    case PEElementKind::Path: {
        const QString d = element.attribute(QStringLiteral("d"));
        if (d.isEmpty()) return false;
        PEPath path = PEPath::fromD(d);
        if (!path.ok()) return false;
        const QVector<PEPathSegment> segs = path.segments();
        QVector<PEPathSegment> out;
        QPointF cursor(0, 0);
        for (const PEPathSegment & s : segs) {
            PEPathSegment ns = s;
            const QPointF oldAnchor = s.anchor(cursor);
            const QPointF newAnchor = oldAnchor + delta;
            if (s.relative && !s.args.isEmpty()) {
                // preserve relative encoding: arg = (target+delta) - (cursor+delta)
                ns.args = { newAnchor.x() - (cursor.x() + delta.x()),
                            newAnchor.y() - (cursor.y() + delta.y()) };
            } else if (s.args.count() >= 2) {
                ns.args = { newAnchor.x(), newAnchor.y() };
            }
            out.append(ns);
            cursor = oldAnchor;
        }
        element.setAttribute(QStringLiteral("d"), PEPath::fromSegments(out).serialize());
        return true;
    }
    case PEElementKind::Group:
    case PEElementKind::Unknown:
    default:
        return false;   // not a geometric primitive
    }
}

// P7: resize a <rect> by replacing its box. Non-rect kinds store only a position (no width/height
// to rescale), so resize is undefined for them at the attribute level -- return false.
bool PEElement::resizeRect(QRectF to) {
    if (kind != PEElementKind::Rect) return false;
    element.setAttribute(QStringLiteral("x"), QString::number(to.x()));
    element.setAttribute(QStringLiteral("y"), QString::number(to.y()));
    element.setAttribute(QStringLiteral("width"), QString::number(to.width()));
    element.setAttribute(QStringLiteral("height"), QString::number(to.height()));
    return true;
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

void PEDocument::flattenInPlace() {
    // P9: bake each *graphic* (leaf) element's *nearest* ancestor <g> transform into its own geometry
    // attrs (absolute user units, i.e. the group's local space -- exactly what the renderer composites),
    // and drop that transform attribute. This is run once at load, before build(); it strips every <g>
    // transform, so re-walking an already-flattened document is an exact no-op (idempotent -- the live
    // initSvgTree re-runs it after every edit with no compounding drift).
    //
    // A <g> is never itself flattened: its transform only contributes *context* to its children, which
    // absorb it. Because a leaf must bake its parent's transform *before* that transform is dropped, we
    // process in DOM order (parents first), so by the time a leaf is baked its nearest transformed
    // ancestor is already the identity and only its own transform (if any) remains to fold in.
    for (PEElement & pe : m_elements) {
        if (pe.kind == PEElementKind::Unknown || pe.kind == PEElementKind::Group) continue;
        if (!PETransform::flatten(pe.element)) continue;
        if (pe.kind == PEElementKind::Rect) {
            bool ok = false;
            double x = pe.element.attribute(QStringLiteral("x")).toDouble(&ok);
            double y = pe.element.attribute(QStringLiteral("y")).toDouble(&ok);
            double w = pe.element.attribute(QStringLiteral("width")).toDouble(&ok);
            double h = pe.element.attribute(QStringLiteral("height")).toDouble(&ok);
            if (ok) {
                pe.bounds = QRectF(x, y, w, h);
                pe.hasBounds = true;
            }
        }
    }
}

QTransform PEDocument::absoluteTransform(const QString & id) const {
    if (id.isEmpty()) return QTransform();
    for (const PEElement & pe : m_elements) {
        if (pe.element.isNull()) continue;
        if (pe.element.attribute(QStringLiteral("id")) != id) continue;
        return PETransform::inheritedTransform(pe.element);
    }
    return QTransform();
}

bool PEDocument::hasTransformedContent() const {
    for (const PEElement & pe : m_elements) {
        if (pe.kind == PEElementKind::Unknown || pe.kind == PEElementKind::Group) continue;
        if (pe.element.isNull()) continue;
        if (!PETransform::inheritedTransform(pe.element).isIdentity()) return true;
    }
    return false;
}
