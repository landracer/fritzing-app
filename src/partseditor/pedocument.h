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

#ifndef PEDOCUMENT_H
#define PEDOCUMENT_H

#include <QString>
#include <QList>
#include <QRectF>
#include <QPointF>
#include <QDomElement>
#include <QTransform>

// P0 (docs/design/vector-editor.md section F): a typed, inert *view* over a part's
// SVG QDomDocument. It does NOT yet edit anything -- it only parses the DOM tree
// into an ordered, typed list of graphic elements, and can serialize that DOM back
// to XML byte-for-byte (the round-trip contract). Later phases (P1+) build the
// editable item model on top of this; for now it must not change app behavior.

// The graphic element kinds the editor models. Mirrors the element tags that
// initSvgTree() (pemainwindow.cpp) already walks; "Unknown" is for any other node
// so we never silently drop a node during a round-trip.
enum class PEElementKind {
    Unknown,
    Group,
    Rect,
    Circle,
    Ellipse,
    Line,
    Polyline,
    Polygon,
    Path,
    Text
};

// A single graphic element in the tree: its kind, its id (the Fritzing convention
// is connectorNpin / connectorNterminal / symbol / schematic ...), the SVG class
// attribute (e.g. "pin", "terminal", "interior rect", "text"), and a cached
// bounding box in user units (may be empty for elements QSvgRenderer cannot bound,
// e.g. text -- same known-Qt limitation the live editor already works around).
struct PEElement {
    PEElementKind kind = PEElementKind::Unknown;
    QString id;
    QString cssClass;
    QRectF bounds;        // in document user units; empty if unknown/unknowable
    bool hasBounds = false;
    QDomElement element;  // the underlying DOM node (view, not copy)

    bool isConnectorPin() const;
    bool isConnectorTerminal() const;
    // "connectorNpin" -> the integer N; returns -1 if not a pin.
    int connectorIndex() const;

    // P2 node editing (pure, no widget): if this element is a <path>, decode its 'd',
    // replace the anchor of segment 'segmentIndex' with 'anchor' (in user units), and
    // return the re-serialized 'd'. Returns an empty string if this element is not a
    // path, the segment index is out of range, or parsing fails.
    QString editPathAnchor(int segmentIndex, QPointF anchor) const;

    // P2/P3 live wiring: the resolved absolute anchor points of this element if it is a
    // path (the grips a node-edit drag would grab); empty list otherwise. Order matches
    // segment index, so segmentIndex in editPathAnchor lines up with this list's index.
    QList<QPointF> pathAnchors() const;

    // P7 (docs/design/vector-editor.md section F): pure move/resize of a graphic. These rewrite the
    // element's own geometry attributes in place (so a grip-drag on a graphic becomes an undoable,
    // model-backed edit rather than just moving the overlay). moveBy translates every coordinate the
    // kind stores: rect x/y, line x1/y1/x2/y2, circle/ellipse cx/cy, text x/y, every polyline/polygon
    // point, and (for a <path>) each decoded segment anchor re-encoded in the source's own absolute/
    // relative convention. It returns true if it rewrote anything (a group/unknown element has no
    // geometry and returns false).
    //
    // resizeRect is defined for a <rect>: it replaces the box outright. The other kinds store only a
    // position, not a size (a line has no width/height to rescale against), so it returns false for
    // them -- resizing those is a non-goal of the attribute-level model (the user drags their anchors).
    bool moveBy(QPointF delta);
    bool resizeRect(QRectF to);
};

// The typed view + round-trip contract. Owns nothing: it borrows the QDomDocument
// it was built from (the live editor keeps QDomDocument as its source of truth).
class PEDocument {
public:
    PEDocument() = default;

    // Parse root's subtree into the typed element list, depth-first and in document
    // order (same order the live initSvgTree walk produces). The document is borrowed
    // (not owned); it must outlive this PEDocument.
    void build(const QDomDocument & doc);

    // Serialize the borrowed document back to an XML string. Pure QDomDocument
    // serialization -- no rewriting -- so a build()/toXml() cycle is byte-stable.
    QString toXml() const;

    // Typed queries.
    const QList<PEElement> & elements() const { return m_elements; }
    int count() const { return m_elements.count(); }
    bool isEmpty() const { return m_elements.isEmpty(); }

    // Find by id (the connector/schematic convention). Returns an element with
    // kind Unknown / null element if absent.
    PEElement findById(const QString & id) const;
    QList<PEElement> byKind(PEElementKind kind) const;
    QList<PEElement> connectorPins() const;

    // P9 (docs/design/vector-editor.md F.5): flatten every modeled element's inherited group transform
    // into its own geometry attrs (PETransform::flatten), in place on the borrowed DOM, then re-read so
    // bounds reflects the now-absolute geometry. Idempotent: a second call is a no-op.
    //
    // DEPRECATED FOR LIVE USE (M0.2). This mutates the borrowed DOM, and flattening is LOSSY: a <circle>
    // under a non-translation drops its r (an ellipse cannot be stored as r), a <path> is re-encoded to
    // absolute, and every ancestor <g transform> is destroyed. Called on a document destined for disk it
    // silently corrupts the part file -- merely opening and saving a part rewrites it. It also breaks
    // editing: moveGraphic/resizeGraphic apply a user-unit delta straight to the element's own attrs,
    // which is only correct because the transform was destroyed.
    //
    // The live editor must instead keep the model document pristine and carry the transform as a
    // property of the item (PEElement::absoluteTransform / PEDocument::absoluteTransform), so display and
    // editing map between local and absolute space. flattenInPlace() remains for pure model tests only.
    void flattenInPlace();

    // M0.2: the absolute user-unit transform 'id' resolves to -- every ancestor <g>'s transform
    // (outermost applied last) times its own transform attribute. Identity when the element is already
    // expressed in absolute coordinates, which is the case for the overwhelming majority of Fritzing
    // part SVGs (they carry no group transforms).
    //
    // This is the non-destructive replacement for flattenInPlace(): the element's geometry attrs stay
    // LOCAL, so the document round-trips byte-for-byte and keeps its group structure, while the live
    // item multiplies this into its scene transform to draw in the right place. Editing maps a delta
    // through it, so a graphic under scale(0.5) moves the right distance instead of half as far.
    // Computed on demand from the live DOM (not cached), so it stays correct after group edits.
    QTransform absoluteTransform(const QString & id) const;

    // M0.2: true when any modeled element resolves to a non-identity transform, i.e. the view actually
    // needs local->absolute mapping. Lets callers keep the existing flat fast path for transform-free
    // documents and pay the mapping cost only where it is real.
    bool hasTransformedContent() const;

    const QDomDocument * domDocument() const { return m_dom; }
    bool isValid() const { return m_dom != nullptr; }

private:
    QDomDocument * m_dom = nullptr;
    QList<PEElement> m_elements;
};

#endif // PEDOCUMENT_H
