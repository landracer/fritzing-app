/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing
Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with Fritzing. If not, see <http://www.gnu.org/licenses/>.
********************************************************************/

#define BOOST_TEST_MODULE PEDocument Tests
#include <boost/test/included/unit_test.hpp>

#include <QDomDocument>
#include <QString>

#include "pedocument.h"
#include "pepath.h"
#include "peprimitive.h"
#include "peconnector.h"

// Parse an SVG string into a QDomDocument (namespace processing, like the live editor).
static void parseDoc(const QString & svg, QDomDocument & doc) {
    QDomDocument::ParseResult result = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(result));
}

BOOST_AUTO_TEST_CASE( round_trip_is_byte_stable )
{
    // A representative Fritzing schematic symbol fragment: a #schematic group with an
    // interior rect, a pin line + its terminal rect, and text labels.
    const QString svg =
        R"(<?xml version='1.0' encoding='UTF-8' standalone='no'?>)" "\n"
        R"(<svg xmlns:svg='http://www.w3.org/2000/svg' xmlns='http://www.w3.org/2000/svg' version='1.2' baseProfile='tiny' width='1.5in' height='0.5in' viewBox='0 0 38.1 12.7' >)" "\n"
        R"(<g id='schematic'>)" "\n"
        R"(<rect class='interior rect' x='5.23875' y='0.15875' width='27.6225' height='12.3825' fill='#FFFFFF' stroke='#000000' />)" "\n"
        R"(<line class='pin' x1='32.8965' y1='2.54' x2='37.9765' y2='2.54' stroke='#787878' id='connector7pin' />)" "\n"
        R"(<rect class='terminal' x='37.9765' y='2.41653' width='0.246944' height='0.246944' id='connector7terminal' />)" "\n"
        R"(<text class='text' x='35.4365' y='2.16958'>8</text>)" "\n"
        R"(</g>)" "\n"
        R"(</svg>)";

    QDomDocument doc;
    parseDoc(svg, doc);

    PEDocument pe;
    pe.build(doc);

    // The typed view should be empty-able back to the exact same bytes (byte-stable).
    BOOST_CHECK_EQUAL(pe.toXml().toStdString(), doc.toString().toStdString());
}

BOOST_AUTO_TEST_CASE( parses_kinds_ids_and_classes )
{
    const QString svg =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='schematic'>)"
        R"(<rect class='interior rect' x='5.23875' y='0.15875' width='27.6225' height='12.3825' />)"
        R"(<line class='pin' id='connector7pin' x1='0' y1='0' x2='1' y2='1' />)"
        R"(<rect class='terminal' id='connector7terminal' x='1' y='1' width='1' height='1' />)"
        R"(<text class='text' x='0' y='0'>8</text>)"
        R"(</g></svg>)";

    QDomDocument doc;
    parseDoc(svg, doc);

    PEDocument pe;
    pe.build(doc);

    // The root <svg> is itself a modelled node (matches live initSvgTree: the svg/g
    // branches fall through and create an item), then group + rect + line + rect + text
    // => 6 elements.
    BOOST_CHECK_EQUAL(pe.count(), 6);

    PEElement schematic = pe.findById("schematic");
    BOOST_CHECK(schematic.kind == PEElementKind::Group);

    PEElement pin = pe.findById("connector7pin");
    BOOST_CHECK(pin.kind == PEElementKind::Line);
    BOOST_CHECK(pin.isConnectorPin());
    BOOST_CHECK_EQUAL(pin.connectorIndex(), 7);
    BOOST_CHECK_EQUAL(pin.cssClass.toStdString(), "pin");

    PEElement terminal = pe.findById("connector7terminal");
    BOOST_CHECK(terminal.isConnectorTerminal());
    BOOST_CHECK_EQUAL(terminal.connectorIndex(), 7);
    // rect bounds read purely from attributes:
    BOOST_CHECK(terminal.hasBounds);
    BOOST_CHECK(terminal.bounds == QRectF(1, 1, 1, 1));

    // connectorPins should find exactly the one pin node.
    BOOST_CHECK_EQUAL(pe.connectorPins().count(), 1);
    BOOST_CHECK_EQUAL(pe.byKind(PEElementKind::Text).count(), 1);
}

BOOST_AUTO_TEST_CASE( unknown_tag_is_not_dropped )
{
    // An element kind we don't model must still round-trip (Unknown, not dropped).
    const QString svg =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><polygon points='0,0 1,1 2,0' /></svg>)";
    QDomDocument doc;
    parseDoc(svg, doc);

    PEDocument pe;
    pe.build(doc);
    // root <svg> (Unknown) + the <polygon> = 2.
    BOOST_CHECK_EQUAL(pe.count(), 2);
    // the polygon (the only modeled-tag among them) is preserved, not dropped.
    bool foundPolygon = false;
    for (const PEElement & e : pe.elements()) {
        if (e.kind == PEElementKind::Polygon) foundPolygon = true;
    }
    BOOST_CHECK(foundPolygon);
    BOOST_CHECK_EQUAL(pe.toXml().toStdString(), doc.toString().toStdString());
}

// P1 invariant: the live editor (initSvgTree) now drives PEGraphicsItem creation off
// PEDocument. That refactor is only behavior-preserving if PEDocument yields exactly the
// same element sequence (depth-first, document order) as the previous inline traverse. This
// test locks that invariant against a nested tree with a mix of modeled and unmodeled tags.
static QStringList manualTraverseOrder(const QDomElement & root) {
    QStringList ids;
    QList<QDomElement> traverse;
    traverse << root;
    while (!traverse.isEmpty()) {
        QDomElement element = traverse.takeFirst();
        ids << element.attribute("id");
        QList<QDomElement> next;
        QDomElement child = element.firstChildElement();
        while (!child.isNull()) {
            next << child;
            child = child.nextSiblingElement();
        }
        while (!next.isEmpty()) {
            traverse.push_front(next.takeLast());
        }
    }
    return ids;
}

BOOST_AUTO_TEST_CASE( order_matches_live_traverse )
{
    // Nesting + mix of modeled/unmodeled tags so any ordering discrepancy is observable.
    const QString svg =
        R"(<svg xmlns='http://www.w3.org/2000/svg' id='r'><g id='g1'><g id='g1a'><rect id='r1' x='0' y='0' width='1' height='1'/></g>)"
        R"(<line id='l1' x1='0' y1='0' x2='1' y2='1'/></g><g id='g2'><circle id='c1' cx='0' cy='0' r='1'/><title id='t1'>x</title></g></svg>)";
    QDomDocument doc;
    parseDoc(svg, doc);

    PEDocument pe;
    pe.build(doc);

    QStringList modelledIds;
    for (const PEElement & e : pe.elements()) {
        if (e.kind == PEElementKind::Unknown) continue;   // the live 'else continue' gate
        modelledIds << e.id;
    }

    // Manual walk: drop unmodeled (svg root + title) tags, keep the rest in order.
    QStringList expected;
    for (const QString & id : manualTraverseOrder(doc.documentElement())) {
        if (id == QLatin1String("r") || id == QLatin1String("t1")) continue; // svg + title not modeled
        expected << id;
    }

    BOOST_CHECK_EQUAL(modelledIds.join(",").toStdString(), expected.join(",").toStdString());
    // modeled (non-Unknown) in document order: g1, g1a, r1, l1, g2, c1
    BOOST_CHECK_EQUAL(modelledIds.count(), 6);
}

// ---- P2: typed path model ('d' <-> segment list round-trip) ----

BOOST_AUTO_TEST_CASE( path_decodes_commands_and_args )
{
    PEPath p = PEPath::fromD("M 1 2 L 3 4 Z");
    BOOST_REQUIRE(p.ok());
    // M, L, Z -> 3 segments
    BOOST_CHECK_EQUAL(p.count(), 3);
    BOOST_CHECK(p.segments().at(0).command == QChar('M'));
    BOOST_CHECK(!p.segments().at(0).relative);
    BOOST_CHECK_EQUAL(p.segments().at(0).args.count(), 2);
    BOOST_CHECK_EQUAL(p.segments().at(0).args.at(0), 1.0);
    BOOST_CHECK_EQUAL(p.segments().at(0).args.at(1), 2.0);
    BOOST_CHECK(p.segments().at(1).command == QChar('L'));
    BOOST_CHECK(p.segments().at(2).command == QChar('Z'));
    BOOST_CHECK_EQUAL(p.segments().at(2).args.count(), 0);
}

BOOST_AUTO_TEST_CASE( path_is_relative_aware )
{
    PEPath p = PEPath::fromD("m 1 2 l 3 4");
    BOOST_REQUIRE(p.ok());
    BOOST_CHECK(p.segments().at(0).relative);   // m
    BOOST_CHECK(p.segments().at(1).relative);   // l
    // anchors: cursor starts 0,0; absolute after absolute is just the literal; relative adds.
    QVector<QPointF> a = p.anchors();
    BOOST_CHECK_EQUAL(a.count(), 2);
    // first (absolute m) => (1,2); second (relative l +3,+4 off cursor 1,2) => (4,6)
    BOOST_CHECK(a.at(0) == QPointF(1, 2));
    BOOST_CHECK(a.at(1) == QPointF(4, 6));
}

// The round-trip contract: decode 'd' -> segments -> serialize -> decode again == same
// segment list. This is what makes node editing lossless: edit a segment, write back 'd'.
BOOST_AUTO_TEST_CASE( path_round_trip_is_stable )
{
    const QString d = "M 0 0 L 10 10 C 12 12 14 14 16 16 Z";
    PEPath p1 = PEPath::fromD(d);
    BOOST_REQUIRE(p1.ok());
    QString d2 = p1.serialize();
    PEPath p2 = PEPath::fromD(d2);
    BOOST_REQUIRE(p2.ok());

    BOOST_REQUIRE_EQUAL(p1.count(), p2.count());
    for (int i = 0; i < p1.count(); ++i) {
        BOOST_CHECK(p1.segments().at(i).command == p2.segments().at(i).command);
        BOOST_CHECK_EQUAL(p1.segments().at(i).relative, p2.segments().at(i).relative);
        BOOST_CHECK(p1.segments().at(i).args == p2.segments().at(i).args);
    }
}

BOOST_AUTO_TEST_CASE( path_invalid_is_not_ok )
{
    // Empty / unparseable path data => ok() false, no segments.
    PEPath p = PEPath::fromD("");
    BOOST_CHECK(!p.ok());
    BOOST_CHECK_EQUAL(p.count(), 0);
}

// ---- P2 interactive: node-edit model op (edit an anchor -> new 'd') ----

static QDomElement pathElement(QDomDocument & doc, const QString & d) {
    QDomElement svg = doc.createElement("svg");
    QDomElement path = doc.createElement("path");
    path.setAttribute("d", d);
    svg.appendChild(path);
    return path;
}

BOOST_AUTO_TEST_CASE( editPathAnchor_absolute_updates_anchor )
{
    QDomDocument doc;
    QDomElement path = pathElement(doc, "M 1 2 L 3 4");
    PEElement pe;
    pe.kind = PEElementKind::Path;
    pe.element = path;

    // absolute path: editing segment 1's anchor replaces its literal coords.
    QString d2 = pe.editPathAnchor(1, QPointF(10, 11));
    BOOST_CHECK(d2.contains("10"));
    BOOST_CHECK(d2.contains("11"));
    // re-decoding the edited 'd' yields a path whose segment 1 anchor is (10,11).
    PEPath re = PEPath::fromD(d2);
    BOOST_REQUIRE(re.ok());
    QVector<QPointF> a = re.anchors();
    BOOST_CHECK(a.at(0) == QPointF(1, 2));
    BOOST_CHECK(a.at(1) == QPointF(10, 11));
}

BOOST_AUTO_TEST_CASE( editPathAnchor_relative_encodes_delta )
{
    QDomDocument doc;
    QDomElement path = pathElement(doc, "M 1 2 l 3 4");
    PEElement pe;
    pe.kind = PEElementKind::Path;
    pe.element = path;

    // segment 1 is relative 'l'; moving its absolute anchor to (20,22) must store the
    // delta relative to the cursor after segment 0 (1,2): (19,20).
    QString d2 = pe.editPathAnchor(1, QPointF(20, 22));
    PEPath re = PEPath::fromD(d2);
    BOOST_REQUIRE(re.ok());
    QVector<QPointF> a = re.anchors();
    BOOST_CHECK(a.at(1) == QPointF(20, 22));
}

BOOST_AUTO_TEST_CASE( editPathAnchor_non_path_returns_empty )
{
    QDomDocument doc;
    QDomElement rect = doc.createElement("rect");
    rect.setAttribute("x", "0");
    PEElement pe;
    pe.kind = PEElementKind::Rect;
    pe.element = rect;
    BOOST_CHECK(pe.editPathAnchor(0, QPointF(1, 1)).isEmpty());
}

// ---- P3: primitive creation (build DOM element -> PEDocument re-reads typed) ----

BOOST_AUTO_TEST_CASE( primitive_create_rect_is_reread_typed )
{
    QDomDocument doc;
    QDomElement svg = doc.createElement("svg");
    doc.appendChild(svg);
    // A rect primitive carries no path geometry in the convenience overload; use make for
    // precise geometry then append, and confirm PEDocument reads it back typed.
    QDomElement rect = PEPrimitive::make(doc, PEElementKind::Rect, "r1", "interior rect",
                                          QRectF(5.23875, 0.15875, 27.6225, 12.3825),
                                          QPointF(), QPointF(), QPointF(), 0.0, QPointF(), 0.0, 0.0,
                                          QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK(rect.tagName().toStdString() == "rect");
    BOOST_CHECK_EQUAL(rect.attribute("id").toStdString(), "r1");
    svg.appendChild(rect);

    PEDocument typed;
    typed.build(doc);
    PEElement readBack = typed.findById("r1");
    BOOST_CHECK(readBack.kind == PEElementKind::Rect);
    BOOST_CHECK(readBack.hasBounds);
    BOOST_CHECK(readBack.bounds == QRectF(5.23875, 0.15875, 27.6225, 12.3825));
    BOOST_CHECK_EQUAL(readBack.cssClass.toStdString(), "interior rect");
}

BOOST_AUTO_TEST_CASE( primitive_create_line_and_circle )
{
    QDomDocument doc;
    QDomElement svg = doc.createElement("svg");
    doc.appendChild(svg);
    QDomElement line = PEPrimitive::make(doc, PEElementKind::Line, "l1", "pin",
                                         QRectF(), QPointF(0, 0), QPointF(1, 1),
                                         QPointF(), 0.0, QPointF(), 0.0, 0.0,
                                         QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK(line.tagName().toStdString() == "line");
    BOOST_CHECK_EQUAL(line.attribute("x1").toStdString(), "0");
    svg.appendChild(line);
    QDomElement circle = PEPrimitive::make(doc, PEElementKind::Circle, "c1", "other",
                                           QRectF(), QPointF(), QPointF(), QPointF(1.397, 1.397), 1.27,
                                           QPointF(), 0.0, 0.0, QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK_EQUAL(circle.attribute("r").toStdString(), "1.27");
    svg.appendChild(circle);

    PEDocument typed;
    typed.build(doc);
    BOOST_CHECK(typed.findById("l1").kind == PEElementKind::Line);
    BOOST_CHECK(typed.findById("c1").kind == PEElementKind::Circle);
}

BOOST_AUTO_TEST_CASE( primitive_addTo_appends_and_roundtrips )
{
    QDomDocument doc;
    QDomElement svg = doc.createElement("svg");
    doc.appendChild(svg);
    QDomElement rect = PEPrimitive::addTo(doc, svg, PEElementKind::Rect, "rect9", "", QRectF(1, 2, 3, 4));
    BOOST_CHECK(rect.tagName().toStdString() == "rect");
    BOOST_CHECK_EQUAL(rect.attribute("fill").toStdString(), "none");

    PEDocument typed;
    typed.build(doc);
    PEElement re = typed.findById("rect9");
    BOOST_CHECK(re.kind == PEElementKind::Rect);
    BOOST_CHECK(re.bounds == QRectF(1, 2, 3, 4));

    // The created element still serializes back cleanly (round-trip on the new subtree).
    BOOST_CHECK_EQUAL(typed.toXml().toStdString(), doc.toString().toStdString());
}

BOOST_AUTO_TEST_CASE( primitive_non_creatable_returns_null_element )
{
    QDomDocument doc;
    QDomElement e = PEPrimitive::make(doc, PEElementKind::Group, "g", "", QRectF(), QPointF(), QPointF(),
                                       QPointF(), 0.0, QPointF(), 0.0, 0.0, QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK(e.isNull());
}

// ---- P3 live wiring: PEElement::pathAnchors() feeds the draggable node grips ----

BOOST_AUTO_TEST_CASE( pathAnchors_returns_segment_end_points )
{
    QDomDocument doc;
    QDomElement path = doc.createElement("path");
    path.setAttribute("id", "p1");
    path.setAttribute("d", "M 0 0 L 10 10 L 20 0 Z");
    QDomElement svg = doc.createElement("svg");
    svg.appendChild(path);
    doc.appendChild(svg);

    PEDocument typed;
    typed.build(doc);
    PEElement pe = typed.findById("p1");
    QList<QPointF> a = pe.pathAnchors();
    // M,L,L,Z -> 4 entries. Z has no args, so its resolved anchor is the unchanged cursor
    // (the last point 20,0) -- the typed model carries no synthetic close anchor.
    BOOST_CHECK_EQUAL(a.count(), 4);
    BOOST_CHECK(a.at(0).x() == 0 && a.at(0).y() == 0);
    BOOST_CHECK(a.at(1).x() == 10 && a.at(1).y() == 10);
    BOOST_CHECK(a.at(2).x() == 20 && a.at(2).y() == 0);
    BOOST_CHECK(a.at(3).x() == 20 && a.at(3).y() == 0);  // Z: anchor unchanged from cursor
}

BOOST_AUTO_TEST_CASE( pathAnchors_relative_resolved_to_absolute )
{
    QDomDocument doc;
    QDomElement path = doc.createElement("path");
    path.setAttribute("id", "p2");
    path.setAttribute("d", "M 5 5 l 3 0 l 0 4");
    QDomElement svg = doc.createElement("svg");
    svg.appendChild(path);
    doc.appendChild(svg);

    PEDocument typed;
    typed.build(doc);
    PEElement pe = typed.findById("p2");
    QList<QPointF> a = pe.pathAnchors();
    BOOST_CHECK_EQUAL(a.count(), 3);
    BOOST_CHECK(a.at(1).x() == 8 && a.at(1).y() == 5);   // 5+3
    BOOST_CHECK(a.at(2).x() == 8 && a.at(2).y() == 9);   // 5+4
}

BOOST_AUTO_TEST_CASE( pathAnchors_non_path_returns_empty )
{
    QDomDocument doc;
    QDomElement rect = doc.createElement("rect");
    rect.setAttribute("id", "r1");
    rect.setAttribute("x", "0");
    QDomElement svg = doc.createElement("svg");
    svg.appendChild(rect);
    doc.appendChild(svg);

    PEDocument typed;
    typed.build(doc);
    PEElement pe = typed.findById("r1");
    BOOST_CHECK(pe.pathAnchors().isEmpty());
}

// ---- P4: connector semantics authored in-app (pure fzp DOM authoring) ----

BOOST_AUTO_TEST_CASE( connector_author_block_is_readable )
{
    QDomDocument doc;
    QDomElement module = doc.createElement("module");
    doc.appendChild(module);

    QDomElement conn = PEConnector::authorConnector(doc, module, "connector1234", "pin 1", "schematic", "G1pin");
    BOOST_CHECK(!conn.isNull());
    BOOST_CHECK_EQUAL(conn.attribute("id").toStdString(), "connector1234");
    BOOST_CHECK_EQUAL(conn.attribute("name").toStdString(), "pin 1");
    // graphic id is readable via the same path the live editor uses
    BOOST_CHECK_EQUAL(PEConnector::graphicSvgId(conn, "schematic").toStdString(), "G1pin");
    // no terminal set -> terminalId empty (centered)
    BOOST_CHECK(PEConnector::terminalId(conn, "schematic").isEmpty());
    // a different view's key has no <p>
    BOOST_CHECK(PEConnector::graphicSvgId(conn, "pcb").isEmpty());
}

BOOST_AUTO_TEST_CASE( connector_rebind_and_terminal_roundtrip )
{
    QDomDocument doc;
    QDomElement module = doc.createElement("module");
    doc.appendChild(module);

    QDomElement conn = PEConnector::authorConnector(doc, module, "connectorA", "", "pcb", "oldpin");
    BOOST_CHECK_EQUAL(PEConnector::graphicSvgId(conn, "pcb").toStdString(), "oldpin");
    BOOST_CHECK(PEConnector::rebindGraphic(conn, "pcb", "newpin"));
    BOOST_CHECK_EQUAL(PEConnector::graphicSvgId(conn, "pcb").toStdString(), "newpin");

    // set a terminalId by editing the <p terminalId> directly, then read it back
    QDomElement views = conn.firstChildElement("views");
    QDomElement view = views.firstChildElement("pcb");
    QDomElement p = view.firstChildElement("p");
    p.setAttribute("terminalId", "G1terminal");
    BOOST_CHECK_EQUAL(PEConnector::terminalId(conn, "pcb").toStdString(), "G1terminal");
}

BOOST_AUTO_TEST_CASE( connector_ensure_creates_container_once )
{
    QDomDocument doc;
    QDomElement module = doc.createElement("module");
    doc.appendChild(module);
    QDomElement c1 = PEConnector::ensureConnectors(module);
    BOOST_CHECK(!c1.isNull());
    BOOST_CHECK_EQUAL(module.elementsByTagName("connectors").count(), 1);
    // ensure again returns the same container, not a second one
    QDomElement c2 = PEConnector::ensureConnectors(module);
    BOOST_CHECK_EQUAL(module.elementsByTagName("connectors").count(), 1);
    Q_UNUSED(c2);
}






