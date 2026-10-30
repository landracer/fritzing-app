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
