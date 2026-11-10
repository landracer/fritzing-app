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

// Golden tests for the sole-renderer editor's pure logic (docs/design/pe-sole-renderer.md):
//   - PESvgPartLoader::colouriseForView : PCB layer colourisation + the nested-copper confine
//   - PEPrimGeom                         : per-kind move/resize geometry and findById
//
// These exercise the engine that drives the interactive editor without needing a QGuiApplication,
// a window system, or the fritzing-parts submodule: a tiny synthetic PCB svg stands in for a real
// part, and the assertions are on the resulting DOM (attributes / colours), the same golden style the
// gerber and svg2gerber suites use.
//
// NOTE (AGENTS.md 5): CONFIG += testcase requires the Q_OBJECT test class to live IN this .cpp so
// qmake's in-source moc rule keys on it; keep the class here, not in a sibling header.

#include <QtTest>
#include <QDomDocument>
#include <QRectF>
#include <QPointF>

#include "pesvgpartloader.h"
#include "peprimitivegeom.h"

// A minimal but faithful PCB svg: an outer copper0 group nesting a copper1 group (the hazard the
// colouriser must confine colour to), a silkscreen group, and a plain <path>. The copper pad starts
// with a non-ink colour so the test can prove the copper layer was matched and recoloured; silkscreen
// is intentionally excluded (it is not a copper/ground-fill layer), so its colour must stay as-is.
static const char * kSynthPcb =
	"<svg xmlns='http://www.w3.org/2000/svg' version='1.2' viewBox='0 0 10 10'>"
	"<g id='copper0'><g id='copper1'>"
	"<circle id='connector0pad' cx='2' cy='2' r='0.5' stroke='#abcdef' fill='none'/>"
	"</g></g>"
	"<g id='silkscreen'>"
	"<rect id='silkbox' x='4' y='4' width='2' height='1' stroke='#123456' fill='#123456'/>"
	"</g>"
	"</svg>";

static QDomElement byId(const QDomDocument & doc, const QString & id)
{
	return PEPrimGeom::findById(doc.documentElement(), id);
}

class TestPESvgEditor : public QObject
{
	Q_OBJECT

private slots:
	void colourise_recognisesPcbLayers();
	void colourise_confinesNestedCopper();
	void colourise_preservesSilkscreenBlackInk();

	void geom_kindFor();
	void geom_localBoundsPerKind();
	void geom_moveCircle();
	void geom_moveRect();
	void geom_movePathIsNoop();
	void geom_resizeRect();
	void geom_resizeRectRejectsNonRect();
	void geom_findById();

	void undoRoundtrip_reapplyReverseDelta();
};

void TestPESvgEditor::colourise_recognisesPcbLayers()
{
	// The copper pad starts as a sentinel (#abcdef). After colourise its stroke MUST become the
	// canonical PCB copper colour (either copper1 gold or copper0 orange, depending on which layer
	// group the colouriser reaches first). That proves the layer group was matched and recoloured.
	QDomDocument raw;
	QVERIFY(raw.setContent(QString::fromUtf8(kSynthPcb)));
	const QDomDocument out = PESvgPartLoader::colouriseForView(raw, PESvgPartLoader::PcbView);

	const QDomElement pad = byId(out, "connector0pad");
	QVERIFY(!pad.isNull());
	const QString stroke = pad.attribute("stroke").toUpper();
	QVERIFY2(stroke == "#FDDE68" || stroke == "#F9A435",
	         qPrintable(QStringLiteral("copper pad stroke not recoloured to a canonical copper hue: %1").arg(stroke)));
}
void TestPESvgEditor::colourise_confinesNestedCopper()
{
	// The pad lives in copper1 nested inside copper0. Each copper layer must end in ITS OWN hue and
	// the outer copper0 must not bleed over the inner. So the inner copper1 group's pad must resolve to
	// the copper1 gold (NOT the copper0 orange). Both hues are valid copper colours, but a bleed would
	// show up as the outer layer's orange reaching the inner pad that copper1 owns.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<g id='copper0'>"
		"<circle id='outerpad' cx='1' cy='1' r='0.5' fill='#eeeeee'/>"
		"<g id='copper1'>"
		"<circle id='innerpad' cx='2' cy='2' r='0.5' fill='#eeeeee'/>"
		"</g>"
		"</g>"
		"</svg>";
	QDomDocument raw;
	QVERIFY(raw.setContent(QString::fromUtf8(svg)));
	const QDomDocument out = PESvgPartLoader::colouriseForView(raw, PESvgPartLoader::PcbView);

	const QString inner = byId(out, "innerpad").attribute("fill").toUpper();
	const QString outer = byId(out, "outerpad").attribute("fill").toUpper();
	// The inner pad belongs to copper1: colouriseForView paints copper1 gold last (after copper0), and
	// the outer group's recursion stops at the nested copper1 boundary, so inner must be gold, not
	// copper0 orange. This is the precise assertion that the nested-copper confine holds.
	QCOMPARE(inner, QString("#FDDE68"));
	// The outer pad is copper0 only and must be the copper0 orange.
	QCOMPARE(outer, QString("#F9A435"));
}



void TestPESvgEditor::colourise_preservesSilkscreenBlackInk()
{
	// Silkscreen IS recoloured -- to the canonical PCB silkscreen colour #000000. A non-ink start
	// colour is rewritten to that hex; and the black-ink exception (stroke/fill "black") is preserved
	// untouched. Both halves are asserted so the golden pins silkscreen's exact behaviour.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 4 4'>"
		"<g id='silkscreen'>"
		"<rect id='r' x='0' y='0' width='1' height='1' fill='#abcabc' stroke='#abcabc'/>"
		"<rect id='ink' x='2' y='0' width='1' height='1' fill='black'/>"
		"</g>"
		"</svg>";
	QDomDocument raw;
	QVERIFY(raw.setContent(QString::fromUtf8(svg)));
	const QDomDocument out = PESvgPartLoader::colouriseForView(raw, PESvgPartLoader::PcbView);

	// Non-ink silkscreen colour -> canonical silkscreen hex.
	const QDomElement r = byId(out, "r");
	QCOMPARE(r.attribute("fill"), QString("#000000"));
	QCOMPARE(r.attribute("stroke"), QString("#000000"));
	// Black ink is in the exception list and must be left exactly as written.
	const QDomElement ink = byId(out, "ink");
	QCOMPARE(ink.attribute("fill"), QString("black"));
}


void TestPESvgEditor::geom_kindFor()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QCOMPARE(PEPrimGeom::kindFor(byId(d, "connector0pad")), PEPrimGeom::Kind::Circle);
	QCOMPARE(PEPrimGeom::kindFor(byId(d, "silkbox")), PEPrimGeom::Kind::Rect);
	QCOMPARE(PEPrimGeom::kindFor(byId(d, "copper0")), PEPrimGeom::Kind::Group);
}

void TestPESvgEditor::geom_localBoundsPerKind()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));

	const QRectF pad = PEPrimGeom::localBounds(byId(d, "connector0pad"));   // cx2 cy2 r0.5
	QVERIFY(pad.isValid());
	QCOMPARE(pad, QRectF(1.5, 1.5, 1.0, 1.0));

	const QRectF box = PEPrimGeom::localBounds(byId(d, "silkbox"));           // x4 y4 w2 h1
	QCOMPARE(box, QRectF(4, 4, 2, 1));
}

void TestPESvgEditor::geom_moveCircle()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement pad = byId(d, "connector0pad");
	QVERIFY(PEPrimGeom::moveBy(pad, QPointF(0.25, -0.5)));
	QCOMPARE(pad.attribute("cx").toDouble(), 2.25);
	QCOMPARE(pad.attribute("cy").toDouble(), 1.5);
}

void TestPESvgEditor::geom_moveRect()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement box = byId(d, "silkbox");
	QVERIFY(PEPrimGeom::moveBy(box, QPointF(1.0, 2.0)));
	QCOMPARE(box.attribute("x").toDouble(), 5.0);
	QCOMPARE(box.attribute("y").toDouble(), 6.0);
	// Size is unchanged by a translate.
	QCOMPARE(box.attribute("width").toDouble(), 2.0);
	QCOMPARE(box.attribute("height").toDouble(), 1.0);
}

void TestPESvgEditor::geom_movePathIsNoop()
{
	// <path> is intentionally not yet translatable (needs PEPath decoding): moveBy must report false
	// and leave the d attribute byte-for-byte identical rather than silently corrupt it.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<path id='p' d='m1,1 2,0 0,2 z'/></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement p = byId(d, "p");
	const QString before = p.attribute("d");
	QVERIFY(!PEPrimGeom::moveBy(p, QPointF(5, 5)));
	QCOMPARE(p.attribute("d"), before);
}

void TestPESvgEditor::geom_resizeRect()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement box = byId(d, "silkbox");
	QVERIFY(PEPrimGeom::resizeRect(box, QRectF(1, 1, 3, 4)));
	QCOMPARE(box.attribute("x").toDouble(), 1.0);
	QCOMPARE(box.attribute("y").toDouble(), 1.0);
	QCOMPARE(box.attribute("width").toDouble(), 3.0);
	QCOMPARE(box.attribute("height").toDouble(), 4.0);
}

void TestPESvgEditor::geom_resizeRectRejectsNonRect()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement pad = byId(d, "connector0pad");
	QVERIFY(!PEPrimGeom::resizeRect(pad, QRectF(0, 0, 1, 1)));
}

void TestPESvgEditor::geom_findById()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QVERIFY(!byId(d, "connector0pad").isNull());   // nested two groups deep
	QVERIFY(!byId(d, "silkbox").isNull());
	QVERIFY(byId(d, "does-not-exist").isNull());
}

void TestPESvgEditor::undoRoundtrip_reapplyReverseDelta()
{
	// The editor's undo stores the move delta and reapplies its negation. Verify the composite of
	// forward + reverse is the identity for a circle -- the property the MoveUndo stack relies on.
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement pad = byId(d, "connector0pad");
	const double cx0 = pad.attribute("cx").toDouble();
	const double cy0 = pad.attribute("cy").toDouble();

	const QPointF delta(0.375, 1.125);
	QVERIFY(PEPrimGeom::moveBy(pad, delta));        // commit forward
	QVERIFY(PEPrimGeom::moveBy(pad, -delta));        // undo = reverse delta

	QCOMPARE(pad.attribute("cx").toDouble(), cx0);
	QCOMPARE(pad.attribute("cy").toDouble(), cy0);
}

QTEST_GUILESS_MAIN(TestPESvgEditor)
#include "test_pesvgeditor.moc"
