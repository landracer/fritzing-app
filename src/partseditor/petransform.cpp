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

#include "petransform.h"

#include <QTransform>
#include <QStringList>
#include <QRegularExpression>
#include <QtMath>   // M_PI, qTan
#include <QPainterPath>
#include <QVariant>
#include <QObject>

#include "pepath.h"
#include "../svg/svgpathlexer.h"
#include "../svg/svgpathparser.h"
#include "../svg/svgpathrunner.h"

// P10: the app's path decode is signal-driven (SVGPathRunner::commandSignal is a SIGNAL, connected via
// the SIGNAL/SLOT string form -- see svgfilesplitter.cpp). This QObject adapter receives each decoded
// command and fills a QPainterPath; it is the receiver for that connect inside shapeFromElement. It lives
// here (the .cpp) so moc keys on this source file -- a Q_OBJECT class declared in a header wired into the
// aggregate build trips the qmake path-keying hazard in AGENTS.md section 5.
class PEPathShapeAdapter : public QObject {
    Q_OBJECT
public:
    explicit PEPathShapeAdapter(QPainterPath * out) : m_out(out) {}
    QPainterPath * m_out = nullptr;
    QPointF m_cursor{0, 0};
    QPointF m_subpathStart{0, 0};
    QChar m_lastCurve;      // 'C'/'S'/'Q'/'T' (upper) for S/T reflection; null otherwise
    QPointF m_lastControl{0, 0};

public Q_SLOTS:
    void onCommand(QChar command, bool relative, QList<double> & args, void * userData);
};

// A number in canonical path/geometry form: trailing-zero-stripped decimal, no exponent, matching
// the QString::number(double) the rest of the pe* plumbing emits.
static QString fmtNum(double d) {
    return QString::number(d);
}

// Forward for the branch dispatch in bakeInto (defined below).
static bool bakeIntoPoints(QDomElement & element, const QTransform & transform, const QString & tag);
static bool bakeIntoPath(QDomElement & element, const QTransform & transform);
static bool bakeIntoText(QDomElement & element, const QTransform & transform);

// Parse an SVG 'transform' string ("translate(1,2) scale(0.5,0.5) matrix(...) ...") into a QTransform
// by applying each primitive left-to-right.
static bool parseTransformList(const QString & ts, QTransform & out) {
    if (ts.trimmed().isEmpty()) return false;

    static const QRegularExpression prim(QStringLiteral(R"((matrix|translate|scale|rotate|skewX|skewY)\s*\(\s*([^)]*)\s*\))"));
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    QTransform result;
    int pos = 0;
    bool any = false;
    while (true) {
        QRegularExpressionMatch match;
        const int found = ts.indexOf(prim, pos, &match);
        if (found < 0) break;
        const QString fn = match.captured(1);
        const QStringList nums = match.captured(2).split(sep, Qt::SkipEmptyParts);
        const auto numAt = [&](int i, double def) { return i < nums.size() ? nums.at(i).toDouble() : def; };
        if (fn == QLatin1String("matrix") && nums.size() >= 6) {
            result = result * QTransform(nums.at(0).toDouble(), nums.at(1).toDouble(),
                                         nums.at(2).toDouble(), nums.at(3).toDouble(),
                                         nums.at(4).toDouble(), nums.at(5).toDouble());
        } else if (fn == QLatin1String("translate")) {
            result = result.translate(numAt(0, 0), numAt(1, 0));
        } else if (fn == QLatin1String("scale")) {
            const double sx = numAt(0, 1);
            result = result.scale(sx, numAt(1, sx));
        } else if (fn == QLatin1String("rotate")) {
            if (nums.size() >= 3) result = result.translate(numAt(0, 0), numAt(1, 0))
                                          .rotate(nums.at(2).toDouble())
                                          .translate(-numAt(0, 0), -numAt(1, 0));
            else                   result = result.rotate(numAt(0, 0));
        } else if (fn == QLatin1String("skewX")) {
            QTransform s; s.shear(qTan(nums.isEmpty() ? 0.0 : nums.at(0).toDouble() * M_PI / 180.0), 0);
            result = result * s;
        } else if (fn == QLatin1String("skewY")) {
            QTransform s; s.shear(0, qTan(nums.isEmpty() ? 0.0 : nums.at(0).toDouble() * M_PI / 180.0));
            result = result * s;
        } else {
            return false;   // unrecognized primitive / wrong arity
        }
        any = true;
        pos = found + match.capturedLength(0);
    }
    if (!any) return false;
    out = result;
    return true;
}

namespace PETransform {

bool parseTransform(const QString & transformString, QTransform & out) {
    return parseTransformList(transformString, out);
}

QTransform inheritedTransform(const QDomElement & element) {
    // Fold the ancestor <g> transforms plus the element's own transform into one absolute mapping.
    //
    // SVG nesting semantics: a local point p is mapped by the element's own transform FIRST, then each
    // ancestor's, innermost to outermost -- abs = G_outer * ... * G_inner * own * p.
    //
    // The walk below goes child->root, so it encounters 'own', then the inner <g>, then the outer <g>.
    // To reach the product above with factors arriving in that order, each new transform must be
    // PRE-multiplied: result = t * result. (The previous code did result = result * t, which composes the
    // chain backwards -- it applied the outermost transform first. Verified against ground truth: for
    // translate(10,20) > scale(2) > rect rotate(90), point (1,1) must map to (-42,22); result=result*t
    // yielded (-22,12).)
    QTransform result;

    const QString own = element.attribute(QStringLiteral("transform"));
    if (!own.isEmpty()) {
        QTransform t;
        if (parseTransformList(own, t)) result = t;   // own transform applies first
    }

    QDomNode node = element.parentNode();
    while (node.isElement()) {
        QDomElement pe = node.toElement();
        const QString ts = pe.attribute(QStringLiteral("transform"));
        QTransform t;
        if (!ts.isEmpty() && parseTransformList(ts, t)) {
            result = t * result;                       // pre-multiply: outer wraps inner
        }
        node = node.parentNode();
    }
    return result;
}

bool bakeInto(QDomElement & element, const QTransform & transform) {
    if (transform.isIdentity()) return false;
    const QString tag = element.tagName();
    if (tag == QLatin1String("rect") || tag == QLatin1String("image")) return bakeIntoPoints(element, transform, tag);
    if (tag == QLatin1String("line")) return bakeIntoPoints(element, transform, tag);
    if (tag == QLatin1String("circle") || tag == QLatin1String("ellipse")) return bakeIntoPoints(element, transform, tag);
    if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) return bakeIntoPoints(element, transform, tag);
    if (tag == QLatin1String("path")) return bakeIntoPath(element, transform);
    if (tag == QLatin1String("text")) return bakeIntoText(element, transform);
    return false;   // group / unknown / unmodelled
}

bool flatten(QDomElement & element) {
    const QTransform t = inheritedTransform(element);
    if (t.isIdentity()) return false;
    if (!bakeInto(element, t)) return false;
    element.removeAttribute(QStringLiteral("transform"));
    return true;
}

}   // namespace PETransform

// ---- per-kind geometry rewrite (fold a resolved transform into an element's own attrs) ----

static bool bakeIntoPoints(QDomElement & element, const QTransform & transform, const QString & tag) {
    if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) {
        const QString points = element.attribute(QStringLiteral("points"));
        if (points.trimmed().isEmpty()) return false;
        static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
        const QStringList nums = points.split(sep, Qt::SkipEmptyParts);
        if (nums.count() < 2 || nums.count() % 2 != 0) return false;
        QStringList out;
        for (int i = 0; i < nums.count(); i += 2) {
            bool ok = false;
            const double x = nums.at(i).toDouble(&ok);
            if (!ok) return false;
            const double y = nums.at(i + 1).toDouble(&ok);
            if (!ok) return false;
            const QPointF p = transform.map(QPointF(x, y));
            out << QStringLiteral("%1,%2").arg(fmtNum(p.x()), fmtNum(p.y()));
        }
        element.setAttribute(QStringLiteral("points"), out.join(QLatin1Char(' ')));
        return true;
    }

    if (tag == QLatin1String("line")) {
        bool ok = false;
        const double x1 = element.attribute(QStringLiteral("x1")).toDouble(&ok); if (!ok) return false;
        const double y1 = element.attribute(QStringLiteral("y1")).toDouble(&ok); if (!ok) return false;
        const double x2 = element.attribute(QStringLiteral("x2")).toDouble(&ok); if (!ok) return false;
        const double y2 = element.attribute(QStringLiteral("y2")).toDouble(&ok); if (!ok) return false;
        const QPointF a = transform.map(QPointF(x1, y1));
        const QPointF b = transform.map(QPointF(x2, y2));
        element.setAttribute(QStringLiteral("x1"), fmtNum(a.x()));
        element.setAttribute(QStringLiteral("y1"), fmtNum(a.y()));
        element.setAttribute(QStringLiteral("x2"), fmtNum(b.x()));
        element.setAttribute(QStringLiteral("y2"), fmtNum(b.y()));
        return true;
    }

    if (tag == QLatin1String("rect") || tag == QLatin1String("image")) {
        bool ok = false;
        const double x = element.attribute(QStringLiteral("x")).toDouble(&ok);
        if (!ok) return false;
        const double y = element.attribute(QStringLiteral("y")).toDouble(&ok);
        if (!ok) return false;
        const double w = element.attribute(QStringLiteral("width")).toDouble(&ok);
        if (!ok) return false;
        const double h = element.attribute(QStringLiteral("height")).toDouble(&ok);
        if (!ok) return false;
        const QRectF r = transform.mapRect(QRectF(x, y, w, h));
        element.setAttribute(QStringLiteral("x"), fmtNum(r.x()));
        element.setAttribute(QStringLiteral("y"), fmtNum(r.y()));
        element.setAttribute(QStringLiteral("width"), fmtNum(r.width()));
        element.setAttribute(QStringLiteral("height"), fmtNum(r.height()));
        return true;
    }

    if (tag == QLatin1String("circle")) {
        bool ok = false;
        const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok);
        if (!ok) return false;
        const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok);
        if (!ok) return false;
        const bool hasR = element.hasAttribute(QStringLiteral("r"));
        const double r = hasR ? element.attribute(QStringLiteral("r")).toDouble(&ok) : 0;
        if (hasR && !ok) return false;
        const QPointF c = transform.map(QPointF(cx, cy));
        element.setAttribute(QStringLiteral("cx"), fmtNum(c.x()));
        element.setAttribute(QStringLiteral("cy"), fmtNum(c.y()));
        // A circle stays a circle only under a pure translate; under scale/rotate/skew the ellipse
        // cannot be stored as an 'r', so the radius is dropped (documented non-lossless case).
        if (hasR && transform.type() <= QTransform::TxTranslate) element.setAttribute(QStringLiteral("r"), fmtNum(r));
        else if (hasR) element.removeAttribute(QStringLiteral("r"));
        return true;
    }

    if (tag == QLatin1String("ellipse")) {
        bool ok = false;
        const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok);
        if (!ok) return false;
        const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok);
        if (!ok) return false;
        const double rx = element.attribute(QStringLiteral("rx")).toDouble(&ok);
        if (!ok) return false;
        const double ry = element.attribute(QStringLiteral("ry")).toDouble(&ok);
        if (!ok) return false;
        const QPointF c = transform.map(QPointF(cx, cy));
        const QPointF xEdge = transform.map(QPointF(cx + rx, cy));
        const QPointF yEdge = transform.map(QPointF(cx, cy + ry));
        element.setAttribute(QStringLiteral("cx"), fmtNum(c.x()));
        element.setAttribute(QStringLiteral("cy"), fmtNum(c.y()));
        element.setAttribute(QStringLiteral("rx"), fmtNum(QLineF(c, xEdge).length()));
        element.setAttribute(QStringLiteral("ry"), fmtNum(QLineF(c, yEdge).length()));
        return true;
    }

    return false;
}

static bool bakeIntoPath(QDomElement & element, const QTransform & transform) {
    const QString d = element.attribute(QStringLiteral("d"));
    PEPath path = PEPath::fromD(d);
    if (!path.ok()) return false;
    // Re-encode every resolved point (anchor + control points) in ABSOLUTE form through the transform.
    // Every segment is emitted absolute (command letters kept, abs/rel normalized to absolute -- documented
    // lossy-in-encoding-but-geometry-true behavior); the rendered geometry is exactly preserved.
    const QVector<PEPathSegment> in = path.segments();
    QVector<PEPathSegment> out;
    QPointF cursor(0, 0);
    for (const PEPathSegment & s : in) {
        PEPathSegment ns;
        if (s.command == QLatin1Char('Z') || s.command == QLatin1Char('z')) {
            ns.command = s.command;   // close has no args
        } else {
            ns.command = s.command.toUpper();
            ns.relative = false;
            const QList<QPointF> pts = s.points(cursor);
            for (const QPointF & p : pts) {
                const QPointF mp = transform.map(p);
                ns.args << mp.x() << mp.y();
            }
        }
        out.append(ns);
        cursor = s.anchor(cursor);
    }
    element.setAttribute(QStringLiteral("d"), PEPath::fromSegments(out).serialize());
    return true;
}

static bool bakeIntoText(QDomElement & element, const QTransform & transform) {
    bool ok = false;
    const double x = element.attribute(QStringLiteral("x")).toDouble(&ok);
    if (!ok) return false;
    const double y = element.attribute(QStringLiteral("y")).toDouble(&ok);
    if (!ok) return false;
    const QPointF p = transform.map(QPointF(x, y));
    element.setAttribute(QStringLiteral("x"), fmtNum(p.x()));
    element.setAttribute(QStringLiteral("y"), fmtNum(p.y()));
    return true;
}

// ---- P10: PEPathShapeAdapter (signal-driven path decode -> QPainterPath) + shapeFromElement ----

void PEPathShapeAdapter::onCommand(QChar command, bool relative, QList<double> & args, void * userData) {
    Q_UNUSED(userData);
    QPainterPath * p = m_out;
    const QPointF c = m_cursor;
    const QChar up = command.toUpper();
    switch (up.toLatin1()) {
    case 'M':
    case 'L': {
        for (int i = 0; i + 1 < args.count(); i += 2) {
            const QPointF q(relative ? QPointF(c.x() + args.at(i), c.y() + args.at(i + 1))
                                      : QPointF(args.at(i), args.at(i + 1)));
            if (up == QLatin1Char('M') && i == 0) {
                p->moveTo(q);
                m_subpathStart = q;
            } else {
                p->lineTo(q);
            }
            m_cursor = q;
        }
        m_lastCurve = QChar();
        break;
    }
    case 'H':
    case 'V': {
        const double x = up == QLatin1Char('H') ? (relative ? c.x() + args.value(0) : args.value(0)) : c.x();
        const double y = up == QLatin1Char('V') ? (relative ? c.y() + args.value(0) : args.value(0)) : c.y();
        const QPointF q(x, y);
        p->lineTo(q);
        m_cursor = q;
        m_lastCurve = QChar();
        break;
    }
    case 'C': {
        for (int i = 0; i + 5 < args.count(); i += 6) {
            const QPointF c1(relative ? c + QPointF(args.at(i), args.at(i + 1)) : QPointF(args.at(i), args.at(i + 1)));
            const QPointF c2(relative ? c + QPointF(args.at(i + 2), args.at(i + 3)) : QPointF(args.at(i + 2), args.at(i + 3)));
            const QPointF q(relative ? c + QPointF(args.at(i + 4), args.at(i + 5)) : QPointF(args.at(i + 4), args.at(i + 5)));
            p->cubicTo(c1, c2, q);
            m_lastControl = c2;
            m_cursor = q;
        }
        m_lastCurve = QLatin1Char('C');
        break;
    }
    case 'S': {
        for (int i = 0; i + 3 < args.count(); i += 4) {
            const QPointF c1 = (m_lastCurve == QLatin1Char('C') || m_lastCurve == QLatin1Char('S'))
                                   ? (2 * m_cursor - m_lastControl) : m_cursor;
            const QPointF c2(relative ? c + QPointF(args.at(i + 2), args.at(i + 3)) : QPointF(args.at(i + 2), args.at(i + 3)));
            const QPointF q(relative ? c + QPointF(args.at(i), args.at(i + 1)) : QPointF(args.at(i), args.at(i + 1)));
            p->cubicTo(c1, c2, q);
            m_lastControl = c2;
            m_cursor = q;
        }
        m_lastCurve = QLatin1Char('S');
        break;
    }
    case 'Q': {
        for (int i = 0; i + 3 < args.count(); i += 4) {
            const QPointF q(relative ? c + QPointF(args.at(i), args.at(i + 1)) : QPointF(args.at(i), args.at(i + 1)));
            const QPointF e(relative ? c + QPointF(args.at(i + 2), args.at(i + 3)) : QPointF(args.at(i + 2), args.at(i + 3)));
            p->quadTo(q, e);
            m_lastControl = q;
            m_cursor = e;
        }
        m_lastCurve = QLatin1Char('Q');
        break;
    }
    case 'T': {
        for (int i = 0; i + 1 < args.count(); i += 2) {
            const QPointF q = (m_lastCurve == QLatin1Char('Q') || m_lastCurve == QLatin1Char('T'))
                                  ? (2 * m_cursor - m_lastControl) : m_cursor;
            const QPointF e(relative ? c + QPointF(args.at(i), args.at(i + 1)) : QPointF(args.at(i), args.at(i + 1)));
            p->quadTo(q, e);
            m_lastControl = q;
            m_cursor = e;
        }
        m_lastCurve = QLatin1Char('T');
        break;
    }
    case 'Z':
        p->closeSubpath();
        m_cursor = m_subpathStart;
        m_lastCurve = QChar();
        break;
    default:
        break;   // 'x' (fake close) / unknown: no-op
    }
}

namespace PETransform {

bool shapeFromElement(const QDomElement & element, QPainterPath & out) {
    const QString tag = element.tagName();
    if (tag == QLatin1String("rect") || tag == QLatin1String("image")) {
        bool ok = false;
        const double x = element.attribute(QStringLiteral("x")).toDouble(&ok); if (!ok) return false;
        const double y = element.attribute(QStringLiteral("y")).toDouble(&ok); if (!ok) return false;
        const double w = element.attribute(QStringLiteral("width")).toDouble(&ok); if (!ok) return false;
        const double h = element.attribute(QStringLiteral("height")).toDouble(&ok); if (!ok) return false;
        out.addRect(QRectF(x, y, w, h));
        return true;
    }
    if (tag == QLatin1String("line")) {
        bool ok = false;
        const double x1 = element.attribute(QStringLiteral("x1")).toDouble(&ok); if (!ok) return false;
        const double y1 = element.attribute(QStringLiteral("y1")).toDouble(&ok); if (!ok) return false;
        const double x2 = element.attribute(QStringLiteral("x2")).toDouble(&ok); if (!ok) return false;
        const double y2 = element.attribute(QStringLiteral("y2")).toDouble(&ok); if (!ok) return false;
        out.moveTo(x1, y1);
        out.lineTo(x2, y2);
        return true;
    }
    if (tag == QLatin1String("circle")) {
        bool ok = false;
        const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok); if (!ok) return false;
        const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok); if (!ok) return false;
        const double r = element.attribute(QStringLiteral("r")).toDouble(&ok); if (!ok) return false;
        out.addEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r));
        return true;
    }
    if (tag == QLatin1String("ellipse")) {
        bool ok = false;
        const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok); if (!ok) return false;
        const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok); if (!ok) return false;
        const double rx = element.attribute(QStringLiteral("rx")).toDouble(&ok); if (!ok) return false;
        const double ry = element.attribute(QStringLiteral("ry")).toDouble(&ok); if (!ok) return false;
        out.addEllipse(QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry));
        return true;
    }
    if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) {
        // Both decode their point-list to a move + line-tos. NOTE: Qt 6 QPainterPath has no close-segment
        // element type, so a <polygon> is not modeled as closed (documented limitation); the geometry is the
        // vertex sequence, which is what the live item paints and hit-tests.
        static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
        const QStringList nums = element.attribute(QStringLiteral("points")).split(sep, Qt::SkipEmptyParts);
        if (nums.count() < 2 || nums.count() % 2 != 0) return false;
        for (int i = 0; i < nums.count(); i += 2) {
            bool ok = false;
            const double x = nums.at(i).toDouble(&ok); if (!ok) return false;
            const double y = nums.at(i + 1).toDouble(&ok); if (!ok) return false;
            if (i == 0) out.moveTo(x, y);
            else        out.lineTo(x, y);
        }
        return true;
    }
    if (tag == QLatin1String("path")) {
        const QString d = element.attribute(QStringLiteral("d"));
        if (d.trimmed().isEmpty()) return false;
        // Decode via the app's own lexer/parser/runner (byte-consistent geometry), then fill the path.
        // commandSignal is a SIGNAL, so it is wired with the SIGNAL/SLOT string form (as elsewhere in
        // the app); PEPathShapeAdapter is the QObject receiver that accumulates the decoded geometry.
        SVGPathLexer lexer(d);
        SVGPathParser parser;
        if (!parser.parse(lexer)) return false;
        QVector<QVariant> stack = parser.symStack();
        SVGPathRunner runner;
        PEPathShapeAdapter adapter(&out);
        QObject::connect(&runner, SIGNAL(commandSignal(QChar, bool, QList<double> &, void *)),
                         &adapter, SLOT(onCommand(QChar, bool, QList<double> &, void *)),
                         Qt::DirectConnection);
        return runner.runPath(stack, nullptr);
    }
    return false;   // unknown / unmodelled
}

}   // namespace PETransform

// moc for the file-local Q_OBJECT PEPathShapeAdapter (class defined above). qmake runs
// "moc -f petransform.cpp -o petransform.moc" for this source; included here so the meta-object lands in
// this TU only (no header-moc, so the aggregate build stays clean per AGENTS.md section 5).
#include "petransform.moc"
