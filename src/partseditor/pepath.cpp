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

#include "pepath.h"

#include <QVariant>
#include <QChar>

#include "../svg/svgpathlexer.h"
#include "../svg/svgpathparser.h"

// A number in the canonical Fritzing path form: trailing-zero-stripped decimal, no
// exponent (matches QString::number(double) used by the rest of the path plumbing).
static QString fmtNum(double d) {
    return QString::number(d);
}

QPointF PEPathSegment::anchor(const QPointF & cursor) const {
    // Pairs are (x, y) in source order. For absolute commands the last pair is the
    // absolute end point; for relative it's a delta off the running cursor.
    if (args.count() >= 2) {
        double x = args.at(args.count() - 2);
        double y = args.at(args.count() - 1);
        if (relative) return QPointF(cursor.x() + x, cursor.y() + y);
        return QPointF(x, y);
    }
    if (command == QLatin1Char('H') || command == QLatin1Char('h')) {
        double dx = args.isEmpty() ? 0 : args.first();
        return QPointF(relative ? cursor.x() + dx : dx, cursor.y());
    }
    if (command == QLatin1Char('V') || command == QLatin1Char('v')) {
        double dy = args.isEmpty() ? 0 : args.first();
        return QPointF(cursor.x(), relative ? cursor.y() + dy : dy);
    }
    return cursor;   // Z/z or degenerate: anchor unchanged
}

QList<QPointF> PEPathSegment::points(const QPointF & cursor) const {
    QList<QPointF> out;
    if (command == QLatin1Char('Z') || command == QLatin1Char('z')) return out;
    if (command == QLatin1Char('H') || command == QLatin1Char('h')) {
        out << QPointF(relative ? cursor.x() + (args.isEmpty() ? 0 : args.first()) : (args.isEmpty() ? 0 : args.first()), cursor.y());
        return out;
    }
    if (command == QLatin1Char('V') || command == QLatin1Char('v')) {
        out << QPointF(cursor.x(), relative ? cursor.y() + (args.isEmpty() ? 0 : args.first()) : (args.isEmpty() ? 0 : args.first()));
        return out;
    }
    // Paired coords.
    for (int i = 0; i + 1 < args.count(); i += 2) {
        if (relative) out << QPointF(cursor.x() + args[i], cursor.y() + args[i + 1]);
        else          out << QPointF(args[i], args[i + 1]);
    }
    return out;
}

PEPath PEPath::fromD(const QString & d) {
    PEPath path;

    QString dataCopy(d);
    if (dataCopy.isEmpty()) return path;
    if (!dataCopy.startsWith(QLatin1Char('M'), Qt::CaseInsensitive)) dataCopy.prepend(QLatin1Char('M'));
    while (dataCopy.endsWith(QLatin1Char(' ')) || dataCopy.endsWith(QLatin1Char('\n')) || dataCopy.endsWith(QLatin1Char('\r')))
        dataCopy.chop(1);
    // Normalize so the grammar has a single entry form (do not synthesize a close marker).
    SVGPathLexer lexer(dataCopy);
    SVGPathParser parser;
    if (!parser.parse(lexer)) {
        return path;   // ok() stays false
    }

    // Walk the symbol stack like SVGPathRunner::runPath, but stop at a real close: collect
    // every command + its arg list into segments (no synthetic close is emitted here).
    QVector<QVariant> & stack = parser.symStack();
    QVector<PEPathSegment> segs;
    PEPathSegment current;
    bool haveCurrent = false;

    for (const QVariant & v : stack) {
        if (v.typeId() == QMetaType::QChar) {
            QChar c = v.toChar();
            // Close markers (real 'z'/'Z') end the current subpath with no args.
            if (haveCurrent) segs.append(current);
            PEPathSegment close;
            close.command = c;
            close.relative = c.isLower();
            // z/Z: no args. Push immediately.
            if (c == QLatin1Char('z') || c == QLatin1Char('Z')) {
                segs.append(close);
                current = PEPathSegment{};
                haveCurrent = false;
                continue;
            }
            current = PEPathSegment{};
            current.command = c;
            current.relative = c.isLower();
            haveCurrent = true;
        } else if (v.typeId() == QMetaType::Double) {
            if (haveCurrent) current.args.append(v.toDouble());
        }
    }
    if (haveCurrent) segs.append(current);

    path.m_segments = segs;
    path.m_ok = true;
    return path;
}

PEPath PEPath::fromSegments(const QVector<PEPathSegment> & segments) {
    PEPath path;
    path.m_segments = segments;
    path.m_ok = true;
    return path;
}

QString PEPath::serialize() const {
    QString out;
    for (const PEPathSegment & s : m_segments) {
        out.append(s.command);
        for (int i = 0; i < s.args.count(); ++i) {
            if (i > 0) out.append(QLatin1Char(' '));
            out.append(fmtNum(s.args.at(i)));
        }
    }
    return out;
}

QVector<QPointF> PEPath::anchors() const {
    QVector<QPointF> out;
    QPointF cursor(0, 0);
    for (const PEPathSegment & s : m_segments) {
        out.append(s.anchor(cursor));
        // advance cursor to this segment's resolved anchor
        cursor = s.anchor(cursor);
    }
    return out;
}
