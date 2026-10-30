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

#ifndef PEPATH_H
#define PEPATH_H

#include <QString>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QRectF>

// P2 (docs/design/vector-editor.md section F): the typed path model that node editing
// builds on. PEPath is a decoded, ordered list of SVG path segments (the segment list);
// it can be serialized back to a 'd' string. This is the pure data half of node editing:
// decode 'd' -> segments, edit a segment's anchor/handle point, re-serialize. It reuses
// the existing SVGPathLexer/SVGPathParser used everywhere else in the app, so parsing is
// byte-for-byte consistent with how Fritzing renders/gerbers a path.
//
// Like PEDocument this is inert for P2 -- no live wiring yet; it only provides a stable,
// testable 'd' <-> segment-list round-trip the later live editor drives off.

// One path segment: a command char (M/L/C/Q/S/T/H/V/Z, upper=absolute lower=relative)
// and its args in source order. Z/z has no args. For absolute M/L etc args[0]=x,
// args[1]=y (and pairs after). For relative, args are deltas (kept verbatim; the model
// preserves the original absolute/relative encoding so a round-trip is lossless).
struct PEPathSegment {
    QChar command;
    bool relative = false;
    QList<double> args;

    // Anchor point for this segment as absolute coords, computed against the running
    // cursor (this is the point a node-editing grip would drag). For H/V the missing
    // axis is filled from the running cursor. Returns the segment's end anchor.
    // 'cursor' is the current pen position before this segment; updated by caller.
    QPointF anchor(const QPointF & cursor) const;
    // All coordinate-ish points this segment contributes (anchor + control/handle points),
    // in source order. Handy for hit-testing grips.
    QList<QPointF> points(const QPointF & cursor) const;
};

class PEPath {
public:
    PEPath() = default;

    // Decode an SVG path 'd' string. Returns a PEPath whose segments() is the segment
    // list. On parse failure returns a PEPath with ok() == false (segments empty).
    static PEPath fromD(const QString & d);

    // Construct a (ok) PEPath directly from a segment list -- the editing path (the live
    // editor edits segments, then re-serializes).
    static PEPath fromSegments(const QVector<PEPathSegment> & segments);

    // Serialize the segment list back to a 'd' string. Stable: serializing a path decoded
    // from 'd' then re-decoding yields an equal segment list (the round-trip contract).
    QString serialize() const;

    const QVector<PEPathSegment> & segments() const { return m_segments; }
    int count() const { return m_segments.count(); }
    bool ok() const { return m_ok; }

    // The end anchor of every segment, resolved to absolute user units.
    QVector<QPointF> anchors() const;

private:
    QVector<PEPathSegment> m_segments;
    bool m_ok = false;
};

#endif // PEPATH_H
