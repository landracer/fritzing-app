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

    const QDomDocument * domDocument() const { return m_dom; }
    bool isValid() const { return m_dom != nullptr; }

private:
    QDomDocument * m_dom = nullptr;
    QList<PEElement> m_elements;
};

#endif // PEDOCUMENT_H
