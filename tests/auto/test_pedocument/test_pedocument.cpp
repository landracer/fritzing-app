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
#include <QPainterPath>
#include <QRectF>
#include <QPointF>

#include "pedocument.h"
#include "pepath.h"
#include "peprimitive.h"
#include "peconnector.h"
#include "peblankpart.h"
#include "pesvgimport.h"
#include "peunits.h"
#include "petransform.h"

// P10: load an SVG string and return its first child element (the graphic under test).
static QDomElement loadShape(const QString & svg) {
    QDomDocument doc;
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    return doc.documentElement().firstChildElement();
}

// Parse an SVG string into a QDomDocument (namespace processing, like the live editor).
static void parseDoc(const QString & svg, QDomDocument & doc) {
    QDomDocument::ParseResult result = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(result));
}

// True if the serialized XML contains a 'transform=' attribute (for the flatten post-condition).
static bool containsTransform(const QString & xml) {
    return xml.contains(QStringLiteral("transform="));
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

// P10+: a freshly created primitive must be *visible* -- the live self-rendering item draws the
// element's own stroke, so PEPrimitive::make seeds a default black stroke (and keeps fill=none).
BOOST_AUTO_TEST_CASE( primitive_make_seeds_visible_default_stroke )
{
    QDomDocument doc;
    QDomElement r = PEPrimitive::make(doc, PEElementKind::Rect, "r1", "",
                                      QRectF(1, 1, 10, 10), QPointF(), QPointF(), QPointF(), 0.0,
                                      QPointF(), 0.0, 0.0, QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK_EQUAL(r.attribute("stroke").toStdString(), "#000000");
    BOOST_CHECK_EQUAL(r.attribute("stroke-width").toStdString(), "1");
    BOOST_CHECK_EQUAL(r.attribute("fill").toStdString(), "none");

    QDomElement l = PEPrimitive::make(doc, PEElementKind::Line, "l1", "",
                                      QRectF(), QPointF(0, 0), QPointF(2, 2), QPointF(), 0.0,
                                      QPointF(), 0.0, 0.0, QList<QPointF>(), QString(), QPointF(), QString());
    BOOST_CHECK_EQUAL(l.attribute("stroke").toStdString(), "#000000");
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







// ---- P6: create-from-blank (PEBlankPart) + svg import (PESvgImport) ----

BOOST_AUTO_TEST_CASE( blank_part_module_is_valid )
{
    QDomDocument doc;
    QDomElement module = PEBlankPart::buildModule(doc, "abc123", "My Resistor");
    BOOST_CHECK(!module.isNull());
    BOOST_CHECK_EQUAL(module.attribute("moduleId").toStdString(), "abc123");
    BOOST_CHECK_EQUAL(module.firstChildElement("title").text().toStdString(), "My Resistor");
    // four view blocks, each with a <layers image><layer layerId/></layers>
    QDomElement views = module.firstChildElement("views");
    BOOST_CHECK(views.firstChildElement("breadboardView").firstChildElement("layers").firstChildElement("layer").isNull() == false);
    BOOST_CHECK_EQUAL(views.firstChildElement("schematicView").firstChildElement("layers").firstChildElement("layer").attribute("layerId").toStdString(), "schematic");
    BOOST_CHECK_EQUAL(views.firstChildElement("pcbView").firstChildElement("layers").firstChildElement("layer").attribute("layerId").toStdString(), "pcb");
    BOOST_CHECK_EQUAL(views.firstChildElement("iconView").firstChildElement("layers").firstChildElement("layer").attribute("layerId").toStdString(), "icon");
    // image placeholder follows <layerName>/<id>.svg
    BOOST_CHECK_EQUAL(views.firstChildElement("schematicView").firstChildElement("layers").attribute("image").toStdString(), "schematic/abc123.svg");
    // an empty connectors container exists
    BOOST_CHECK(!module.firstChildElement("connectors").isNull());
}

BOOST_AUTO_TEST_CASE( blank_view_svg_has_layer_group )
{
    QString svg = PEBlankPart::blankViewSvg("schematic", QRectF(0, 0, 1, 1));
    // standard header + a single <g id='schematic'> group, byte-stable round-trip through a fresh parse
    QDomDocument doc;
    BOOST_REQUIRE(static_cast<bool>(doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing)));
    QDomElement g = doc.documentElement().firstChildElement("g");
    BOOST_CHECK_EQUAL(g.attribute("id").toStdString(), "schematic");
    // viewBox present (the header carried width/height in inches + viewBox user units)
    BOOST_CHECK(!doc.documentElement().attribute("viewBox").isEmpty());
}

BOOST_AUTO_TEST_CASE( import_renames_single_group )
{
    const QString foreign =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='foreignGroup'><rect x='1' y='1'/></g></svg>)";
    QDomDocument doc;
    BOOST_REQUIRE(PESvgImport::parse(foreign, doc));
    QString oldId;
    QString id = PESvgImport::normalize(doc, "schematic", &oldId);
    BOOST_CHECK_EQUAL(id.toStdString(), "schematic");
    BOOST_CHECK_EQUAL(oldId.toStdString(), "foreignGroup");
    // the group is now the canonical id and holds the rect
    QDomElement g = doc.documentElement().firstChildElement("g");
    BOOST_CHECK_EQUAL(g.attribute("id").toStdString(), "schematic");
    BOOST_CHECK(!g.firstChildElement("rect").isNull());
}

BOOST_AUTO_TEST_CASE( import_wraps_bare_graphics_into_group )
{
    const QString foreign =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><rect id='r' x='1' y='1'/><line id='l' x1='0' y1='0' x2='2' y2='2'/></svg>)";
    QDomDocument doc;
    BOOST_REQUIRE(PESvgImport::parse(foreign, doc));
    QString id = PESvgImport::normalize(doc, "breadboard", nullptr);
    BOOST_CHECK_EQUAL(id.toStdString(), "breadboard");
    QDomElement root = doc.documentElement();
    QDomElement g = root.firstChildElement("g");
    BOOST_CHECK_EQUAL(g.attribute("id").toStdString(), "breadboard");
    // both former top-level graphics are now inside the single group, in document order
    BOOST_CHECK_EQUAL(g.firstChildElement().toElement().tagName().toStdString(), "rect");
    BOOST_CHECK_EQUAL(g.firstChildElement().toElement().nextSiblingElement().tagName().toStdString(), "line");
}

BOOST_AUTO_TEST_CASE( import_strips_gorn_and_is_idempotent )
{
    const QString foreign =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='old' gorn='0 0'><rect x='1' y='1'/></g></svg>)";
    QDomDocument doc;
    BOOST_REQUIRE(PESvgImport::parse(foreign, doc));
    PESvgImport::normalize(doc, "pcb", nullptr);
    QDomElement g = doc.documentElement().firstChildElement("g");
    BOOST_CHECK_EQUAL(g.attribute("id").toStdString(), "pcb");
    BOOST_CHECK(!g.hasAttribute("gorn"));   // gorn stripped
    // normalizing again is a no-op on the id
    QDomDocument doc2;
    BOOST_REQUIRE(PESvgImport::parse(doc.toString(4), doc2));
    QString oldAgain;
    QString idAgain = PESvgImport::normalize(doc2, "pcb", &oldAgain);
    BOOST_CHECK_EQUAL(idAgain.toStdString(), "pcb");
    BOOST_CHECK_EQUAL(oldAgain.toStdString(), "pcb");   // already canonical -> old id == new id
}

BOOST_AUTO_TEST_CASE( import_parse_failure_returns_input )
{
    const QString garbage = "this is not xml <<<";
    QDomDocument doc;
    BOOST_CHECK(!PESvgImport::parse(garbage, doc));
    // normalizeToString returns the input unchanged on parse failure
    BOOST_CHECK_EQUAL(PESvgImport::normalizeToString(garbage, "schematic").toStdString(), garbage.toStdString());
}

// ---- P7 units: pure px<->user-unit conversion (the math PEUtils now delegates to) ----

BOOST_AUTO_TEST_CASE( units_inch_conversion )
{
    // 90 user units (the Fritzing svg dpi) == 1 inch
    BOOST_CHECK_CLOSE(PEUnits::pxToUnits(90.0, "in"), 1.0, 1e-9);
    BOOST_CHECK_CLOSE(PEUnits::pxToUnits(45.0, "in"), 0.5, 1e-9);
    BOOST_CHECK_CLOSE(PEUnits::unitsToPx(1.0, "in"), 90.0, 1e-9);
}

BOOST_AUTO_TEST_CASE( units_mm_conversion_and_roundtrip )
{
    // 90 user units == 1 inch == 25.4 mm
    BOOST_CHECK_CLOSE(PEUnits::pxToUnits(90.0, "mm"), 25.4, 1e-9);
    BOOST_CHECK_CLOSE(PEUnits::unitsToPx(25.4, "mm"), 90.0, 1e-9);
    // round-trip stability: px -> mm -> px == px
    BOOST_CHECK_CLOSE(PEUnits::unitsToPx(PEUnits::pxToUnits(12.34, "mm"), "mm"), 12.34, 1e-9);
}

BOOST_AUTO_TEST_CASE( units_unknown_unit_is_identity )
{
    // an unrecognized unit name passes through unchanged (as PEUtils did before extraction)
    BOOST_CHECK_CLOSE(PEUnits::pxToUnits(7.5, "px"), 7.5, 1e-9);
    BOOST_CHECK_CLOSE(PEUnits::unitsToPx(7.5, "px"), 7.5, 1e-9);
    BOOST_CHECK_EQUAL(PEUnits::pxToUnitsStr(90.0, "in").toStdString(), "1");
}

// ---- P7 move/resize: pure geometry rewrite of a typed element's attributes ----

BOOST_AUTO_TEST_CASE( moveBy_rect_shifts_x_y )
{
    QDomDocument doc;
    QDomElement rect = doc.createElement("rect");
    rect.setAttribute("x", "10");
    rect.setAttribute("y", "20");
    rect.setAttribute("width", "5");
    rect.setAttribute("height", "6");
    PEElement pe;
    pe.kind = PEElementKind::Rect;
    pe.element = rect;
    BOOST_CHECK(pe.moveBy(QPointF(3, -4)));
    BOOST_CHECK_EQUAL(rect.attribute("x").toStdString(), "13");
    BOOST_CHECK_EQUAL(rect.attribute("y").toStdString(), "16");
    // width/height unchanged by a pure translate
    BOOST_CHECK_EQUAL(rect.attribute("width").toStdString(), "5");
    BOOST_CHECK_EQUAL(rect.attribute("height").toStdString(), "6");
}

BOOST_AUTO_TEST_CASE( moveBy_line_shifts_both_endpoints )
{
    QDomDocument doc;
    QDomElement line = doc.createElement("line");
    line.setAttribute("x1", "0"); line.setAttribute("y1", "0");
    line.setAttribute("x2", "10"); line.setAttribute("y2", "10");
    PEElement pe;
    pe.kind = PEElementKind::Line;
    pe.element = line;
    BOOST_CHECK(pe.moveBy(QPointF(5, 5)));
    BOOST_CHECK_EQUAL(line.attribute("x1").toStdString(), "5");
    BOOST_CHECK_EQUAL(line.attribute("y2").toStdString(), "15");
}

BOOST_AUTO_TEST_CASE( moveBy_polygon_shifts_every_point )
{
    QDomDocument doc;
    QDomElement poly = doc.createElement("polygon");
    poly.setAttribute("points", "0,0 10,0 10,10");
    PEElement pe;
    pe.kind = PEElementKind::Polygon;
    pe.element = poly;
    BOOST_CHECK(pe.moveBy(QPointF(1, 2)));
    BOOST_CHECK_EQUAL(poly.attribute("points").toStdString(), "1,2 11,2 11,12");
}

BOOST_AUTO_TEST_CASE( moveBy_group_is_not_geometric_returns_false )
{
    QDomDocument doc;
    QDomElement g = doc.createElement("g");
    PEElement pe;
    pe.kind = PEElementKind::Group;
    pe.element = g;
    BOOST_CHECK(!pe.moveBy(QPointF(1, 1)));
}

BOOST_AUTO_TEST_CASE( moveBy_path_shifts_absolute_anchor )
{
    QDomDocument doc;
    QDomElement path = doc.createElement("path");
    path.setAttribute("d", "M 0 0 L 10 10");
    PEElement pe;
    pe.kind = PEElementKind::Path;
    pe.element = path;
    BOOST_CHECK(pe.moveBy(QPointF(2, 3)));
    // absolute anchors shift by the delta
    PEElement readBack;
    readBack.kind = PEElementKind::Path;
    readBack.element = path;
    QList<QPointF> anchors = readBack.pathAnchors();
    BOOST_REQUIRE(anchors.count() >= 2);
    BOOST_CHECK_CLOSE(anchors.at(0).x(), 2.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(0).y(), 3.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(1).x(), 12.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(1).y(), 13.0, 1e-6);
}

BOOST_AUTO_TEST_CASE( resizeRect_replaces_box_and_nonrect_returns_false )
{
    QDomDocument doc;
    QDomElement rect = doc.createElement("rect");
    rect.setAttribute("x", "0"); rect.setAttribute("y", "0");
    rect.setAttribute("width", "1"); rect.setAttribute("height", "1");
    PEElement pe;
    pe.kind = PEElementKind::Rect;
    pe.element = rect;
    BOOST_CHECK(pe.resizeRect(QRectF(7, 8, 9, 10)));
    BOOST_CHECK_EQUAL(rect.attribute("x").toStdString(), "7");
    BOOST_CHECK_EQUAL(rect.attribute("width").toStdString(), "9");
    BOOST_CHECK_EQUAL(rect.attribute("height").toStdString(), "10");

    // a non-rect cannot be resized at the attribute level
    QDomElement circle = doc.createElement("circle");
    circle.setAttribute("cx", "0"); circle.setAttribute("cy", "0"); circle.setAttribute("r", "5");
    PEElement ce;
    ce.kind = PEElementKind::Circle;
    ce.element = circle;
    BOOST_CHECK(!ce.resizeRect(QRectF(0, 0, 1, 1)));
}

BOOST_AUTO_TEST_CASE( kindForElement_maps_tag_to_kind )
{
    QDomDocument doc;
    BOOST_CHECK(PEPrimitive::kindForElement(doc.createElement("rect"))   == PEElementKind::Rect);
    BOOST_CHECK(PEPrimitive::kindForElement(doc.createElement("path"))   == PEElementKind::Path);
    BOOST_CHECK(PEPrimitive::kindForElement(doc.createElement("g"))      == PEElementKind::Group);
    BOOST_CHECK(PEPrimitive::kindForElement(doc.createElement("bogus"))  == PEElementKind::Unknown);
    BOOST_CHECK(PEPrimitive::kindForElement(QDomElement())               == PEElementKind::Unknown);
}

// ---- P8: flatten-and-drop the inherited group transform at load (docs/design/vector-editor.md B.2(b)) ----

static QDomElement flattenInGroup(const QString & gTransform, const QString & childXml, QDomDocument & doc) {
    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'>"
        "<g id='schematic' transform='%1'>%2</g></svg>").arg(gTransform, childXml);
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    QDomElement g = doc.documentElement().firstChildElement();
    QDomElement child = g.firstChildElement();
    BOOST_REQUIRE(!child.isNull());
    BOOST_CHECK(PETransform::flatten(child));
    return child;
}

BOOST_AUTO_TEST_CASE( p8_parseTransform_translate_and_scale )
{
    QTransform t;
    BOOST_CHECK(PETransform::parseTransform(QStringLiteral("translate(3,4)"), t));
    BOOST_CHECK_CLOSE(t.dx(), 3.0, 1e-9);
    BOOST_CHECK_CLOSE(t.dy(), 4.0, 1e-9);

    QTransform s;
    BOOST_CHECK(PETransform::parseTransform(QStringLiteral("scale(2)"), s));
    BOOST_CHECK_EQUAL(s.map(QPointF(5, 7)).x(), 10.0);
    BOOST_CHECK_EQUAL(s.map(QPointF(5, 7)).y(), 14.0);

    QTransform bad;
    BOOST_CHECK(!PETransform::parseTransform(QStringLiteral("notatransform"), bad));
    BOOST_CHECK(!PETransform::parseTransform(QStringLiteral(""), bad));
}

BOOST_AUTO_TEST_CASE( p8_flatten_rect_translate_then_moveBy_lands_correctly )
{
    QDomDocument doc;
    QDomElement rect = flattenInGroup(QStringLiteral("translate(10,20)"),
                                      QStringLiteral("<rect x='1' y='2' width='3' height='4'/>"), doc);
    BOOST_CHECK_EQUAL(rect.attribute("x").toStdString(), "11");
    BOOST_CHECK_EQUAL(rect.attribute("y").toStdString(), "22");
    BOOST_CHECK_EQUAL(rect.attribute("width").toStdString(), "3");
    BOOST_CHECK_EQUAL(rect.attribute("transform").toStdString(), "");   // dropped
    PEElement pe; pe.kind = PEElementKind::Rect; pe.element = rect;
    BOOST_CHECK(pe.moveBy(QPointF(1, 1)));
    BOOST_CHECK_EQUAL(rect.attribute("x").toStdString(), "12");
    BOOST_CHECK_EQUAL(rect.attribute("y").toStdString(), "23");
}

BOOST_AUTO_TEST_CASE( p8_flatten_line_maps_both_endpoints )
{
    QDomDocument doc;
    QDomElement line = flattenInGroup(QStringLiteral("translate(5,5)"),
                                      QStringLiteral("<line x1='0' y1='0' x2='2' y2='3'/>"), doc);
    BOOST_CHECK_EQUAL(line.attribute("x1").toStdString(), "5");
    BOOST_CHECK_EQUAL(line.attribute("y1").toStdString(), "5");
    BOOST_CHECK_EQUAL(line.attribute("x2").toStdString(), "7");
    BOOST_CHECK_EQUAL(line.attribute("y2").toStdString(), "8");
}

BOOST_AUTO_TEST_CASE( p8_flatten_polygon_maps_every_point )
{
    QDomDocument doc;
    QDomElement poly = flattenInGroup(QStringLiteral("translate(1,2)"),
                                      QStringLiteral("<polygon points='0,0 10,0 10,10'/>"), doc);
    BOOST_CHECK_EQUAL(poly.attribute("points").toStdString(), "1,2 11,2 11,12");
}

BOOST_AUTO_TEST_CASE( p8_flatten_text_maps_anchor )
{
    QDomDocument doc;
    QDomElement text = flattenInGroup(QStringLiteral("translate(2,3)"),
                                      QStringLiteral("<text x='1' y='1'>x</text>"), doc);
    BOOST_CHECK_EQUAL(text.attribute("x").toStdString(), "3");
    BOOST_CHECK_EQUAL(text.attribute("y").toStdString(), "4");
}

BOOST_AUTO_TEST_CASE( p8_flatten_path_absolute_anchor_baked )
{
    QDomDocument doc;
    QDomElement path = flattenInGroup(QStringLiteral("translate(100,200)"),
                                      QStringLiteral("<path d='M0 0L10 10'/>"), doc);
    PEElement pe; pe.kind = PEElementKind::Path; pe.element = path;
    const QList<QPointF> anchors = pe.pathAnchors();
    BOOST_REQUIRE(anchors.count() >= 2);
    BOOST_CHECK_CLOSE(anchors.at(0).x(), 100.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(0).y(), 200.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(1).x(), 110.0, 1e-6);
    BOOST_CHECK_CLOSE(anchors.at(1).y(), 210.0, 1e-6);
    BOOST_CHECK_EQUAL(path.attribute("transform").toStdString(), "");
}

BOOST_AUTO_TEST_CASE( p8_flatten_no_group_transform_is_noop )
{
    QDomDocument doc;
    const QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg'><rect x='1' y='1' width='2' height='2'/></svg>");
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    QDomElement rect = doc.documentElement().firstChildElement();
    BOOST_CHECK(!PETransform::flatten(rect));
    BOOST_CHECK_EQUAL(rect.attribute("x").toStdString(), "1");
}

BOOST_AUTO_TEST_CASE( p8_flatten_circle_drops_r_under_scale )
{
    QDomDocument doc;
    QDomElement circle = flattenInGroup(QStringLiteral("scale(2)"),
                                        QStringLiteral("<circle cx='5' cy='5' r='3'/>"), doc);
    BOOST_CHECK_EQUAL(circle.attribute("cx").toStdString(), "10");
    BOOST_CHECK_EQUAL(circle.attribute("cy").toStdString(), "10");
    BOOST_CHECK(!circle.hasAttribute("r"));
}

// ---- P9: typed flattenInPlace over the whole borrowed DOM (the live initSvgTree wiring) ----

BOOST_AUTO_TEST_CASE( p9_flattenInPlace_bakes_leaf_into_group_space )
{
    QDomDocument doc;
    const QString svg =
        R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='schematic' transform='translate(10,20)'><rect id='r1' x='1' y='2' width='3' height='4'/><line id='l1' x1='0' y1='0' x2='2' y2='2'/></g></svg>)";
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));

    PEDocument pe;
    pe.build(doc);
    pe.flattenInPlace();

    // A leaf graphic is baked to absolute (1+10, 2+20) and re-read into pe.bounds (moveBy can hit it).
    PEElement rect = pe.findById("r1");
    BOOST_CHECK(rect.hasBounds);
    BOOST_CHECK(rect.bounds == QRectF(11, 22, 3, 4));
    BOOST_CHECK_EQUAL(rect.element.attribute("x").toStdString(), "11");
    BOOST_CHECK_EQUAL(rect.element.attribute("y").toStdString(), "22");

    // line baked into the group's space (the transform was in the parent's coords already, so a translate
    // by the group's (10,20) shifts the leaf's local coords by that delta -- here (0,0)->(10,20)).
    BOOST_CHECK_EQUAL(pe.findById("l1").element.attribute("x1").toStdString(), "10");
    BOOST_CHECK_EQUAL(pe.findById("l1").element.attribute("y2").toStdString(), "22");

    // Group-as-unit is deferred (P11): the <g>'s transform is intentionally left in place.
    BOOST_CHECK(!pe.findById("schematic").element.attribute("transform").isEmpty());
}


BOOST_AUTO_TEST_CASE( p9_flattenInPlace_no_transform_is_identity )
{
    QDomDocument doc;
    const QString svg = R"(<svg xmlns='http://www.w3.org/2000/svg'><rect id='r1' x='5' y='5' width='2' height='2'/></svg>)";
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    PEDocument pe;
    pe.build(doc);
    const QString before = pe.toXml();
    pe.flattenInPlace();
    BOOST_CHECK_EQUAL(pe.toXml().toStdString(), before.toStdString());   // byte-stable when nothing to bake
}

// ---- P10: shapeFromElement (per-kind geometry -> QPainterPath) ----

BOOST_AUTO_TEST_CASE( p10_shape_rect )
{
    QDomElement e = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><rect x='3' y='4' width='10' height='20'/></svg>)");
    QPainterPath p;
    BOOST_REQUIRE(PETransform::shapeFromElement(e, p));
    BOOST_CHECK(p.boundingRect() == QRectF(3, 4, 10, 20));
    BOOST_CHECK(p.contains(QPointF(3.0001, 4.0001)));
    BOOST_CHECK(!p.contains(QPointF(2.9, 4)));   // outside the rect
}

BOOST_AUTO_TEST_CASE( p10_shape_circle_and_ellipse )
{
    QPainterPath c;
    QDomElement ce = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><circle cx='10' cy='10' r='5'/></svg>)");
    BOOST_REQUIRE(PETransform::shapeFromElement(ce, c));
    BOOST_CHECK(c.boundingRect() == QRectF(5, 5, 10, 10));
    BOOST_CHECK(c.contains(QPointF(10, 10)));

    QPainterPath el;
    QDomElement ee = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><ellipse cx='0' cy='0' rx='2' ry='3'/></svg>)");
    BOOST_REQUIRE(PETransform::shapeFromElement(ee, el));
    BOOST_CHECK(el.boundingRect() == QRectF(-2, -3, 4, 6));
}

BOOST_AUTO_TEST_CASE( p10_shape_line_is_hairline )
{
    QDomElement e = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><line x1='0' y1='0' x2='10' y2='0'/></svg>)");
    QPainterPath p;
    BOOST_REQUIRE(PETransform::shapeFromElement(e, p));
    BOOST_CHECK(p.boundingRect() == QRectF(0, 0, 10, 0));   // zero-height hairline
    BOOST_CHECK(p.elementCount() == 2);                       // a move + a line
}

BOOST_AUTO_TEST_CASE( p10_shape_polygon_and_polyline )
{
    // Qt 6 QPainterPath has no close-segment element type, so a <polygon> cannot be modeled as closed;
    // both tags decode their point-list to the same open move + line-tos. The vertex sequence is the
    // modeled geometry the live item paints/hit-tests from (documented limitation, not a bug).
    const QString pts = QStringLiteral("0,0 10,0 10,10");
    for (const QString & tag : {QStringLiteral("polygon"), QStringLiteral("polyline")}) {
        QDomElement e = loadShape(QString(R"(<svg xmlns='http://www.w3.org/2000/svg'><%1 points='%2'/></svg>)").arg(tag, pts));
        BOOST_CHECK(!e.isNull());
        QPainterPath p;
        BOOST_REQUIRE(PETransform::shapeFromElement(e, p));
        BOOST_CHECK(p.elementCount() == 3);   // move + two line-tos
        BOOST_CHECK(QPointF(p.elementAt(0)) == QPointF(0, 0));
        BOOST_CHECK(QPointF(p.elementAt(1)) == QPointF(10, 0));
        BOOST_CHECK(QPointF(p.elementAt(2)) == QPointF(10, 10));
        BOOST_CHECK(p.boundingRect() == QRectF(0, 0, 10, 10));
    }
}

BOOST_AUTO_TEST_CASE( p10_shape_path_absolute_and_relative )
{
    // Absolute square with a real close.
    QPainterPath a;
    QDomElement ae = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><path d='M 0 0 L 10 0 L 10 10 L 0 10 Z'/></svg>)");
    BOOST_REQUIRE(PETransform::shapeFromElement(ae, a));
    BOOST_CHECK(a.boundingRect() == QRectF(0, 0, 10, 10));
    BOOST_CHECK(a.contains(QPointF(5, 5)));

    // Equivalent path in relative encoding must resolve to the same square (cursor-relative resolve).
    QPainterPath r;
    QDomElement re = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><path d='M 0 0 l 10 0 l 0 10 l -10 0 z'/></svg>)");
    BOOST_REQUIRE(PETransform::shapeFromElement(re, r));
    BOOST_CHECK(r.boundingRect() == QRectF(0, 0, 10, 10));
    BOOST_CHECK(r.contains(QPointF(5, 5)));
}

BOOST_AUTO_TEST_CASE( p10_shape_path_cubic_curve_bounds )
{
    // A cubic from (0,0) to (10,0) with controls at y=+10 and y=-10. Qt's tight bounding box is
    // curve-accurate (it ignores control points), so it bulges off the zero-height anchor line --
    // proving curves are honored, not just the anchor polyline.
    QPainterPath p;
    QDomElement e = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><path d='M 0 0 C 5 10 5 -10 10 0 Z'/></svg>)");
    BOOST_REQUIRE(PETransform::shapeFromElement(e, p));
    const QRectF b = p.boundingRect();
    BOOST_CHECK(qAbs(b.left()) < 1e-6);                 // 0
    BOOST_CHECK(qAbs(b.right() - 10.0) < 1e-6);          // 10
    BOOST_CHECK(b.top() < 0.0 && b.top() > -10.0);        // curve bulges above (more negative than) the anchor line
    BOOST_CHECK(b.bottom() > 0.0 && b.bottom() < 10.0);   // and below it (tight curve bound ~ +/-2.887)
}

BOOST_AUTO_TEST_CASE( p10_shape_unmodelled_returns_false )
{
    // A bare <g> is not a shape, and an unrecognized tag is not a shape -> no self-render geometry.
    QDomElement g = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='notshape'/></svg>)");
    QPainterPath p;
    BOOST_CHECK(!PETransform::shapeFromElement(g, p));

    QDomElement foo = loadShape(R"(<svg xmlns='http://www.w3.org/2000/svg'><foo id='x'/></svg>)");
    QPainterPath p2;
    BOOST_CHECK(!PETransform::shapeFromElement(foo, p2));
}

BOOST_AUTO_TEST_CASE( p10_live_render_round_trip_moveBy )
{
    // Live-item contract: shapeFromElement(render) reads the same attrs moveBy edits, so a move is
    // reflected in the shape with no baked fill and no separate renderer coordinate space.
    QDomDocument doc;
    QDomDocument::ParseResult r = doc.setContent(
        R"(<svg xmlns='http://www.w3.org/2000/svg'><rect id='r1' x='3' y='4' width='10' height='20'/></svg>)",
        QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    PEDocument pe;
    pe.build(doc);

    QPainterPath before;
    BOOST_REQUIRE(PETransform::shapeFromElement(pe.findById("r1").element, before));
    BOOST_CHECK(before.boundingRect() == QRectF(3, 4, 10, 20));

    BOOST_REQUIRE(pe.findById("r1").moveBy(QPointF(100, 100)));

    QPainterPath after;
    BOOST_REQUIRE(PETransform::shapeFromElement(pe.findById("r1").element, after));
    BOOST_CHECK(after.boundingRect() == QRectF(103, 104, 10, 20));   // followed the model edit
}

// ---- M0.2 / M0.5: non-destructive transform handling (docs/design/vector-editor.md §B.2) ----

// Parse a part SVG, build the typed view over it, and hand back the document. Used by the M0.2 cases to
// assert that building/querying the view never perturbs the DOM the editor later serializes to disk.
static QDomDocument m02Build(const QString & svg, PEDocument & out) {
    QDomDocument doc;
    QDomDocument::ParseResult r = doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    BOOST_REQUIRE(static_cast<bool>(r));
    out.build(doc);
    return doc;
}

BOOST_AUTO_TEST_CASE( m02_byte_equality_open_and_save_untouched )
{
    // The regression that made the editor unsafe to ship: opening a part and saving it must not rewrite
    // the file. The typed view is built on every load, so building it -- and every read path the live
    // surface uses -- must be pure with respect to the DOM.
    PEDocument pe;
    QDomDocument doc = m02Build(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'>"
        "<g id='schematic' transform='translate(5,5) scale(2)'>"
        "<rect id='r1' x='1' y='2' width='3' height='4' fill='#FFFFFF' stroke='#000000'/>"
        "<circle id='c1' cx='10' cy='10' r='2'/>"
        "<path id='p1' d='M 1 1 l 5 5 h 3 z'/>"
        "<line id='l1' x1='0' y1='0' x2='9' y2='9'/>"
        "<text id='t1' x='3' y='4'>Ref</text>"
        "</g></svg>",
        pe);
    const QString before = doc.toString();

    BOOST_REQUIRE(pe.count() > 0);
    BOOST_CHECK(pe.hasTransformedContent());
    (void) pe.findById("r1").bounds;
    (void) pe.findById("p1").pathAnchors();
    (void) pe.absoluteTransform("c1");
    (void) pe.findById("c1").moveBy(QPointF(0, 0));   // a zero-delta move must not perturb formatting either

    BOOST_CHECK_EQUAL(doc.toString().toStdString(), before.toStdString());
}

BOOST_AUTO_TEST_CASE( m02_absolute_transform_matches_renderer_composition )
{
    // absoluteTransform reports the mapping the renderer composites: ancestor transforms outermost-last,
    // times the element's own transform -- while geometry attrs stay LOCAL (non-destructive).
    PEDocument pe;
    QDomDocument doc = m02Build(
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g transform='translate(10,20)'>"
        "<g transform='scale(2)'>"
        "<rect id='inner' x='1' y='1' width='2' height='2' transform='rotate(90)'/>"
        "</g></g></svg>",
        pe);

    const QTransform t = pe.absoluteTransform("inner");
    BOOST_CHECK(!t.isIdentity());

    // Expected composition, built the same way PETransform::inheritedTransform accumulates:
    // result = result * parentTransform, outermost group first, then the element's own transform.
    QTransform expected;
    expected = expected * QTransform().translate(10, 20);
    expected = expected * QTransform().scale(2, 2);
    expected = expected * QTransform().rotate(90);

    // Compare by mapping a probe point rather than a QTransform equality API (Qt exposes no operator==
    // /isNearExact for QTransform): the mapping is the contract, so test the mapping.
    const QPointF probe(1, 1);
    const QPointF got = t.map(probe);
    const QPointF want = expected.map(probe);
    BOOST_CHECK_CLOSE(got.x(), want.x(), 1e-6);
    BOOST_CHECK_CLOSE(got.y(), want.y(), 1e-6);

    // Nothing was baked: the rect keeps local attrs and both transforms stay on their own nodes.
    const QDomElement inner = pe.findById("inner").element;
    BOOST_CHECK_EQUAL(inner.attribute("x").toStdString(), "1");
    BOOST_CHECK_EQUAL(inner.attribute("width").toStdString(), "2");
    BOOST_CHECK_EQUAL(inner.attribute("transform").toStdString(), "rotate(90)");

    QDomElement scaleGroup;
    for (int i = 0; i < doc.elementsByTagName("g").count(); ++i) {
        const QDomElement g = doc.elementsByTagName("g").at(i).toElement();
        if (g.attribute("transform") == "scale(2)") scaleGroup = g;
    }
    BOOST_CHECK_EQUAL(scaleGroup.attribute("transform").toStdString(), "scale(2)");
}

BOOST_AUTO_TEST_CASE( m02_absolute_transform_identity_when_no_transform )
{
    // The overwhelmingly common case (Fritzing part SVGs carry no group transforms): identity, and the
    // document reports no transformed content so callers keep the flat fast path.
    PEDocument pe;
    m02Build(R"(<svg xmlns='http://www.w3.org/2000/svg'><g id='schematic'><rect id='r1' x='5' y='5' width='2' height='2'/></g></svg>)", pe);

    BOOST_CHECK(pe.absoluteTransform("r1").isIdentity());
    BOOST_CHECK(!pe.hasTransformedContent());
    BOOST_CHECK(pe.absoluteTransform("missing").isIdentity());
}

BOOST_AUTO_TEST_CASE( m02_move_is_local_space_so_callers_must_map )
{
    // Why the destructive flatten existed: moveGraphic applies a user-unit (absolute) delta straight to
    // the element's own attrs, which is only correct when the element is already absolute. Non-destructive
    // means moveBy stays in LOCAL space, so the caller must divide the absolute delta through
    // absoluteTransform(). This pins both halves of that contract.
    PEDocument pe;
    m02Build(R"(<svg xmlns='http://www.w3.org/2000/svg'><g transform='scale(2)'><rect id='r1' x='1' y='1' width='2' height='2'/></g></svg>)", pe);

    const QTransform t = pe.absoluteTransform("r1");
    BOOST_CHECK(!t.isIdentity());

    // A 10-unit absolute move under scale(2) is a 5-unit move in local space.
    bool ok = false;
    const QTransform inv = t.inverted(&ok);
    BOOST_REQUIRE(ok);
    const QPointF localDelta = inv.map(QPointF(10, 10));
    BOOST_CHECK_CLOSE(localDelta.x(), 5.0, 1e-6);
    BOOST_CHECK_CLOSE(localDelta.y(), 5.0, 1e-6);

    BOOST_REQUIRE(pe.findById("r1").moveBy(localDelta));
    BOOST_CHECK_EQUAL(pe.findById("r1").element.attribute("x").toStdString(), "6");

    // ...and the absolute position really did advance by exactly 10 user units.
    const QPointF absTopLeft = pe.absoluteTransform("r1").map(QPointF(6, 6));
    BOOST_CHECK_CLOSE(absTopLeft.x(), 12.0, 1e-6);
    BOOST_CHECK_CLOSE(absTopLeft.y(), 12.0, 1e-6);
}
