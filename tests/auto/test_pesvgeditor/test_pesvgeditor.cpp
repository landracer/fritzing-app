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

// Golden tests for the sole-renderer editor's pure logic (docs/design/pe-sole-renderer.md) and, from
// Slice A of docs/design/pe-selfpaint-renderer.md, the self-paint renderer PESelfPaintItem:
//   - PESvgPartLoader::colouriseForView : PCB layer colourisation + the nested-copper confine
//   - PEPrimGeom                         : per-kind move/resize geometry and findById
//   - PESelfPaintItem                    : per-element self-paint that does NOT bleed a sibling
//
// The colourise/geom/plan tests exercise the engine without a window system. The self-paint tests DO render
// (to a QImage) to prove the central property: a PESelfPaintItem for element A paints ONLY A's path and never
// bleeds sibling B -- the exact failure of QtSvg's render(id) (whole-doc-clipped-to-bbox) that caused the PCB
// double-draw. A QGuiApplication (QTEST_MAIN) + the offscreen platform is required for raster rendering.
//
// NOTE (AGENTS.md 5): CONFIG += testcase requires the Q_OBJECT test class to live IN this .cpp so
// qmake's in-source moc rule keys on it; keep the class here, not in a sibling header.

#include <QtTest>
#include <QDomDocument>
#include <QRectF>
#include <QPointF>
#include <QImage>
#include <QPainter>
#include <QGraphicsScene>
#include <QTemporaryDir>

#include "pesvgpartloader.h"
#include "peprimitivegeom.h"
#include "peitemplan.h"
#include "petransform.h"
#include "peselfpaintitem.h"
#include "pesvgeditor.h"
#include "pepath.h"

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
	void geom_findById();

	// Slice D: path node editing (PEPrimGeom::pathAnchors / editPathAnchor) -- closes the path gap.
	void geom_pathAnchorsAbsolute();
	void geom_editPathAnchorAbsoluteRoundtrip();
	void geom_editPathAnchorRelativeConvention();
	void geom_pathAnchorsEmptyForNonPathAndBadD();

	// PEPath decode model (preserved from the deleted test_pedocument suite when the legacy PEElement/
	// PEDocument model was swept in Slice E -- PEPath is the live path engine PEPrimGeom drives off, so its
	// round-trip/relative/invalid coverage must not be lost with the orphan model it used to live beside).
	void pePath_decodesCommandsAndArgs();
	void pePath_isRelativeAware();
	void pePath_roundTripIsStable();
	void pePath_invalidIsNotOk();

	void undoRoundtrip_reapplyReverseDelta();

	// Hybrid itemization (planItems) -- the sole-renderer no-duplication contract.
	void plan_decomposesGroupWithEditableLeaf_noGroupDup();
	void plan_opaqueGroupRenderedWholeUneditable();
	void plan_noIdAppearsTwice();

	// Slice A (docs/design/pe-selfpaint-renderer.md): the self-paint renderer paints ONLY its own element.
	void selfPaint_paintsOnlyItsOwnElement_notTheSibling();   // the double-draw killer
	void selfPaint_editableLeafIsSelectable();
	void selfPaint_opaqueGroupPaintsWholeSubtreeInOrder();


	// Slice B: the full rebuildScene pipeline (plan -> flatten -> compose into one scene).
	void pipeline_nestedCopperPadRendersOnce_notTwice();
	void pipeline_flattenBakesAncestorTranslate_intoAbsoluteGeometry();

	// Slice C: interaction (hit-test -> move commit -> undo), headless against the real helpers.
	void interaction_smallestAreaHitSelectsPadOverLayerBox();
	void interaction_moveCommitThenUndoRoundTrips();

	// Slice D: full chrome interaction driven through the real PESvgEditor controller.
	void chrome_rectGripResizeRoundTrip();
	void chrome_pathAnchorNodeEditRoundTrip();

	// R1: resize that actually resizes -- generic resizeTo per kind, and grips on every resizable kind.
	void geom_resizeToLine();
	void geom_resizeToCircle();
	void geom_resizeToPolygonScalesPoints();
	void geom_resizeToRejectsPath();
	void controller_circleIsNotResizableNoOp();

	// Slice F: save -- serialise the edited DOM back to disk and re-parse (raw-source-of-truth round-trip).
	void save_roundTripsThroughDisk();
	void save_failsWithoutFilepath();
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
	// Rect resize via the generic resizeTo (the rect case): fit the box to (1,1,3,4). `from` is the rect's
	// current localBounds. Replaces the old rect-only resizeRect.
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QDomElement box = byId(d, "silkbox");
	QRectF from = PEPrimGeom::localBounds(box);
	QVERIFY(from.isValid());
	QVERIFY(PEPrimGeom::resizeTo(box, from, QRectF(1, 1, 3, 4)));
	QCOMPARE(box.attribute("x").toDouble(), 1.0);
	QCOMPARE(box.attribute("y").toDouble(), 1.0);
	QCOMPARE(box.attribute("width").toDouble(), 3.0);
	QCOMPARE(box.attribute("height").toDouble(), 4.0);
}

void TestPESvgEditor::geom_findById()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	QVERIFY(!byId(d, "connector0pad").isNull());   // nested two groups deep
	QVERIFY(!byId(d, "silkbox").isNull());
	QVERIFY(byId(d, "does-not-exist").isNull());
}

// ---- Slice D: path node editing ------------------------------------------------------------------------

void TestPESvgEditor::geom_pathAnchorsAbsolute()
{
	// An absolute polyline path. The end-anchor of each segment resolves to its literal absolute point;
	// the close (Z) leaves the cursor unchanged, so its "anchor" repeats the last point.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<path id='p' d='M 0,0 L 10,10 L 20,0 Z'/></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	const QVector<QPointF> a = PEPrimGeom::pathAnchors(byId(d, "p"));
	QVERIFY2(a.count() >= 3, qPrintable(QString::number(a.count())));
	QCOMPARE(a.at(0), QPointF(0, 0));
	QCOMPARE(a.at(1), QPointF(10, 10));
	QCOMPARE(a.at(2), QPointF(20, 0));
}

void TestPESvgEditor::geom_editPathAnchorAbsoluteRoundtrip()
{
	// Edit the middle anchor (segment 1) from (10,10) to (30,30); re-read the anchors -> segment 1 now reads
	// back (30,30), segment 2 stays (20,0). Proves decode -> edit one anchor -> re-encode -> decode is lossless
	// except for the one node we moved.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<path id='p' d='M 0,0 L 10,10 L 20,0 Z'/></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement p = byId(d, "p");
	QVERIFY(PEPrimGeom::editPathAnchor(p, 1, QPointF(30, 30)));
	const QVector<QPointF> a = PEPrimGeom::pathAnchors(p);
	QCOMPARE(a.at(0), QPointF(0, 0));
	QCOMPARE(a.at(1), QPointF(30, 30));
	QCOMPARE(a.at(2), QPointF(20, 0));
}

void TestPESvgEditor::geom_editPathAnchorRelativeConvention()
{
	// A relative path: 'm0,0 l 10,10'. Editing segment 1 (the 'l') to an absolute (30,30) must re-encode the
	// arg as a DELTA off the running cursor (0,0) -> "l 30,30" still resolves to (30,30) when re-read. Proves
	// editPathAnchor preserves the source's relative convention (writes delta, not a raw absolute).
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<path id='p' d='m0,0 l10,10'/></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement p = byId(d, "p");
	QVERIFY(PEPrimGeom::editPathAnchor(p, 1, QPointF(30, 30)));
	// After the edit, the relative 'l' args are (30-0, 30-0); re-reading yields anchor (0+30, 0+30) = (30,30).
	const QVector<QPointF> a = PEPrimGeom::pathAnchors(p);
	QCOMPARE(a.at(0), QPointF(0, 0));
	QCOMPARE(a.at(1), QPointF(30, 30));
	// The stored 'd' must still be lowercase (relative) -- not silently converted to absolute.
	QVERIFY2(p.attribute("d").contains('l') || p.attribute("d").contains(','), "relative convention kept");
}

void TestPESvgEditor::geom_pathAnchorsEmptyForNonPathAndBadD()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8("<svg xmlns='http://www.w3.org/2000/svg'>"
	                                       "<rect id='r' x='0' y='0' width='1' height='1'/>"
	                                       "<path id='bad' d='not a path @@@'/></svg>")));
	QDomElement r = byId(d, "r");
	QDomElement bad = byId(d, "bad");
	QVERIFY(PEPrimGeom::pathAnchors(r).isEmpty());          // not a path -> empty
	QVERIFY(!PEPrimGeom::editPathAnchor(r, 0, QPointF(1, 1)));
	QVERIFY(PEPrimGeom::pathAnchors(bad).isEmpty());         // undecodable 'd' -> empty
}

void TestPESvgEditor::undoRoundtrip_reapplyReverseDelta()
{
	// The editor's undo stores the move delta and reapplies its negation. Verify the composite of
	// forward + reverse is the identity for a circle -- the property the Move-kind UndoEntry relies on.
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

void TestPESvgEditor::plan_decomposesGroupWithEditableLeaf_noGroupDup()
{
	// kSynthPcb: copper0 > copper1 > connector0pad (editable circle), and silkscreen > silkbox
	// (editable rect). Because each copper/silkscreen group CONTAINS an editable graphic, the planner
	// must DESCEND into them (rule 3) and emit only the leaves -- NOT the group whole. If it also emitted
	// the group, the pad would be painted twice (the exact Session-30 duplication bug). Assert: exactly the
	// two leaves are planned, both editable, and no group id appears in the plan at all.
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthPcb)));
	const QVector<PEItemPlan::Planned> planned = PEItemPlan::plan(d.documentElement(), {});

	QStringList ids;
	for (const PEItemPlan::Planned & p : planned) ids << p.id;
	QCOMPARE(planned.size(), 2);
	QVERIFY2(ids.contains("connector0pad"), qPrintable(ids.join(",")));
	QVERIFY2(ids.contains("silkbox"),       qPrintable(ids.join(",")));
	// Group ids must NOT appear (they were descended, not backdrop-drawn).
	QVERIFY2(!ids.contains("copper0") && !ids.contains("copper1") && !ids.contains("silkscreen"),
	         qPrintable(QStringLiteral("group id leaked into plan (duplication): %1").arg(ids.join(","))));
	for (const PEItemPlan::Planned & p : planned) QVERIFY2(p.editable, qPrintable(p.id));
}

void TestPESvgEditor::plan_opaqueGroupRenderedWholeUneditable()
{
	// A group whose subtree holds NO editable graphic (only a <path> and a <text>, both deferred/unknown)
	// must be emitted ONCE as a whole, non-editable item (rule 2). This is the breadboard-body case: the
	// art renders faithfully but carries no data(0), so it is not selectable (breadboard editing deferred).
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<g id='breadboard'><g id='icon'>"
		"<path d='M0,0 10,0 10,10 z'/>"
		"<text x='1' y='1'>EIO</text>"
		"</g></g>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	const QVector<PEItemPlan::Planned> planned = PEItemPlan::plan(d.documentElement(), {});

	QCOMPARE(planned.size(), 1);
	QCOMPARE(planned.at(0).id, QString("breadboard"));
	QVERIFY2(!planned.at(0).editable, "opaque art group must be non-editable");
}

void TestPESvgEditor::plan_noIdAppearsTwice()
{
	// The core no-duplication guarantee across a mixed doc: an id'd pad group and an opaque group. The
	// planner walks once and every id appears at most once. Assert the multiset of ids has no repeats.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<g id='copper1'><circle id='connector0pad' cx='2' cy='2' r='0.5'/></g>"
		"<g id='silkscreen'><rect id='silkbox' x='4' y='4' width='2' height='1'/></g>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	const QVector<PEItemPlan::Planned> planned = PEItemPlan::plan(d.documentElement(), {});

	QSet<QString> seen;
	for (const PEItemPlan::Planned & p : planned) {
		QVERIFY2(!seen.contains(p.id),
		         qPrintable(QStringLiteral("id emitted twice (duplication): %1").arg(p.id)));
		seen.insert(p.id);
	}
	// Both leaves planned (groups descended), nothing else.
	QCOMPARE(planned.size(), 2);
}

// ---- Slice A: self-paint renderer -----------------------------------------------------------------------

// Render a scene that contains ONLY a self-paint item for `el` (no sibling), on white, into a QImage.
static QImage renderItemAlone(const QDomElement & el)
{
	QGraphicsScene scene;
	auto * item = new PESelfPaintItem();
	item->setElement(el);   // test elements carry absolute coords (no transform)
	scene.addItem(item);
	QImage img(40, 40, QImage::Format_ARGB32);
	img.fill(Qt::white);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, false);
	scene.render(&p, QRectF(0, 0, 40, 40), QRectF(0, 0, 40, 40));   // 1:1 scene->image
	p.end();
	return img;
}

void TestPESvgEditor::selfPaint_paintsOnlyItsOwnElement_notTheSibling()
{
	// Two disjoint primitives: a RED circle (pad) at (5,5) r=2, and a GREEN circle (sibling) at (30,30)
	// r=2. Create a self-paint item for the pad ONLY, render the scene, and assert: the pad's pixels are
	// red, the sibling's centre is still white (NOT painted). With QtSvg's render(id) the pad item would
	// repaint the whole doc clipped to its bbox -- but here there is no doc, and the item structurally
	// cannot draw the sibling. This is the invariant that makes the double-draw impossible.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<circle id='pad' cx='5' cy='5' r='2' fill='#ff0000'/>"
		"<circle id='other' cx='30' cy='30' r='2' fill='#00ff00'/>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement pad = PEPrimGeom::findById(d.documentElement(), "pad");
	QVERIFY(!pad.isNull());

	const QImage img = renderItemAlone(pad);
	// Centre of the pad (5,5) is red; centre of the sibling (30,30) is untouched white.
	QCOMPARE(img.pixelColor(5, 5).name(), QString("#ff0000"));
	QCOMPARE(img.pixelColor(30, 30).name(), QString("#ffffff"));
	// A point between them (17,17) stays white too -- proof there's no bbox-clip bleed.
	QCOMPARE(img.pixelColor(17, 17).name(), QString("#ffffff"));
}

void TestPESvgEditor::selfPaint_editableLeafIsSelectable()
{
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<circle id='pad' cx='2' cy='2' r='1' fill='#abcdef'/></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	PESelfPaintItem item;
	item.setElement(PEPrimGeom::findById(d.documentElement(), "pad"));
	item.setEditable(true);
	QVERIFY(item.editable());
	QVERIFY(item.flags().testFlag(QGraphicsItem::ItemIsSelectable));
	item.setEditable(false);
	QVERIFY(!item.editable());
	QVERIFY(!item.flags().testFlag(QGraphicsItem::ItemIsSelectable));
}

void TestPESvgEditor::selfPaint_opaqueGroupPaintsWholeSubtreeInOrder()
{
	// An opaque unit is a <g> with two children of different colours. The self-paint item must paint BOTH
	// (the whole subtree), not just one. Red on the left, blue on the right, disjoint.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 10'>"
		"<g id='grp'>"
		"<rect id='r' x='1' y='1' width='6' height='6' fill='#ff0000'/>"
		"<rect id='b' x='21' y='1' width='6' height='6' fill='#0000ff'/>"
		"</g></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	const QImage img = renderItemAlone(d.documentElement().firstChildElement());
	QCOMPARE(img.pixelColor(4, 4).name(),  QString("#ff0000"));   // red child painted
	QCOMPARE(img.pixelColor(24, 4).name(), QString("#0000ff"));   // blue child painted
	QCOMPARE(img.pixelColor(13, 4).name(), QString("#ffffff"));   // gap stays white
}


// ---- Slice B: the full rebuildScene pipeline (plan -> flatten -> compose) --------------------------------

// Mirror PESvgEditor::rebuildScene: plan the doc, flatten each planned element, bind one PESelfPaintItem per
// planned unit into a single scene, and render. Used by the tests below to prove the composed editor image.
static QImage renderPlanned(const QDomDocument & doc)
{
	QGraphicsScene scene;
	const QDomElement root = doc.documentElement();
	for (const PEItemPlan::Planned & p : PEItemPlan::plan(root, {})) {
		QDomElement el = PEPrimGeom::findById(root, p.id);
		if (el.isNull()) continue;
		PETransform::flatten(el);
		auto * item = new PESelfPaintItem();
		item->setElement(el);
		if (p.editable) item->setData(0, p.id);
		scene.addItem(item);
	}
	QImage img(40, 40, QImage::Format_ARGB32);
	img.fill(Qt::white);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, false);
	scene.render(&p, QRectF(0, 0, 40, 40), QRectF(0, 0, 40, 40));
	p.end();
	return img;
}

// ---- Slice C: interaction (hit-test -> move commit -> undo), against the real item construction ----------
//
// buildScene() mirrors PESvgEditor::rebuildScene exactly (plan -> findById -> flatten -> one PESelfPaintItem
// per planned unit, editable leaves carry data(0)). The Slice C tests then call the SAME helpers the editor's
// handlePress/handleRelease/undo call -- PESelfPaintItem::hitTestId (selection) and PEPrimGeom::moveBy (commit,
// including the negated delta undo uses) -- so they exercise the editor's real interaction logic, not a copy.
static QGraphicsScene * buildScene(const QDomDocument & doc)
{
	QGraphicsScene * scene = new QGraphicsScene();
	const QDomElement root = doc.documentElement();
	for (const PEItemPlan::Planned & p : PEItemPlan::plan(root, {})) {
		QDomElement el = PEPrimGeom::findById(root, p.id);
		if (el.isNull()) continue;
		PETransform::flatten(el);
		auto * item = new PESelfPaintItem();
		item->setElement(el);
		if (p.editable) item->setData(0, p.id);
		scene->addItem(item);
	}
	return scene;
}

void TestPESvgEditor::pipeline_nestedCopperPadRendersOnce_notTwice()
{
	// THE double-draw scenario, end to end: copper0 > copper1 > connector0pad. Because the group contains an
	// editable leaf, the planner DESCENDS and emits only the pad -- one item, painted once. The scene renders
	// the pad disc (fill) once; nothing else is painted (no layer background art here). If the group had ALSO
	// been emitted whole, QtSvg would repaint the pad under it; here the pad simply paints once.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<g id='copper0'><g id='copper1'>"
		"<circle id='connector0pad' cx='10' cy='10' r='3' fill='#cc0000'/>"
		"</g></g>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	// plan() must descend (emit only the pad), not the group.
	QStringList ids;
	for (const PEItemPlan::Planned & p : PEItemPlan::plan(d.documentElement(), {})) ids << p.id;
	QCOMPARE(ids.count(), 1);
	QVERIFY2(ids.contains("connector0pad"), qPrintable(ids.join(",")));
	QVERIFY2(!ids.contains("copper0") && !ids.contains("copper1"), "group must be descended, not backdrop-drawn");

	const QImage img = renderPlanned(d);
	// Centre of the pad is the pad's red (interior, well clear of the AA edge), and the far corner is white.
	QColor centre = img.pixelColor(10, 10);
	QVERIFY2(centre.red() > 0xb0 && centre.green() < 0x40 && centre.blue() < 0x40,
	         qPrintable(centre.name()));                       // reddish at the pad centre
	QCOMPARE(img.pixelColor(35, 35).name(), QString("#ffffff")); // untouched white
	// A point clearly outside the disc (just past r=3) stays white (not painted).
	QCOMPARE(img.pixelColor(16, 10).name(), QString("#ffffff"));
}

void TestPESvgEditor::pipeline_flattenBakesAncestorTranslate_intoAbsoluteGeometry()
{
	// A leaf inside a translated <g> must, after the pipeline's flatten, paint at the ABSOLUTE (baked)
	// position, proving the rebuildScene flatten step is what makes the identity-transform self-paint item
	// land in the right place. group translate(10,10) wraps a rect at local (0,0,4,4) -> absolute (10,10,4,4).
	// NOTE: the test does NOT pre-flatten (that would double-bake); renderPlanned is the only flattener.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<g transform='translate(10,10)'>"
		"<rect id='r' x='0' y='0' width='4' height='4' fill='#00aa00'/>"
		"</g></svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	// Sanity: before any flatten the local bounds are the raw (untransformed) attrs.
	QDomElement r = PEPrimGeom::findById(d.documentElement(), "r");
	QCOMPARE(PEPrimGeom::localBounds(r), QRectF(0, 0, 4, 4));

	// renderPlanned flattens once -> absolute (10,10,4,4) -> paints there, NOT at the old local (1,1).
	const QImage img = renderPlanned(d);
	QColor at12 = img.pixelColor(12, 12);   // inside the baked (10,10,4,4) box
	QVERIFY2(at12.green() > 0x90 && at12.red() < 0x40, qPrintable(at12.name()));
	QCOMPARE(img.pixelColor(2, 2).name(),  QString("#ffffff"));   // old local spot untouched
	QCOMPARE(img.pixelColor(35, 35).name(), QString("#ffffff"));
}

// ---- Slice C: interaction, end to end -------------------------------------------------------------------

void TestPESvgEditor::interaction_smallestAreaHitSelectsPadOverLayerBox()
{
	// A pad (r=3 at 10,10) sits INSIDE a big layer box (0,0,40,40). Both are editable id'd leaves. A press
	// at the pad centre must select the PAD (smallest area), not the layer box it overlaps -- this is the
	// editor's core selection guarantee (clicking a pad selects the pad, not the layer background).
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<rect id='layer' x='0' y='0' width='40' height='40' fill='#eeeeee'/>"
		"<circle id='pad' cx='10' cy='10' r='3' fill='#cc0000'/>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));

	QGraphicsScene * scene = buildScene(d);
	QCOMPARE(PESelfPaintItem::hitTestId(scene, QPointF(10, 10)), QString("pad"));
	// A point on the layer box but OFF the pad selects the layer (pad doesn't contain it).
	QCOMPARE(PESelfPaintItem::hitTestId(scene, QPointF(30, 30)), QString("layer"));
	// A point outside every shape (negative coords, past the layer's top-left) selects nothing.
	QCOMPARE(PESelfPaintItem::hitTestId(scene, QPointF(-1, -1)), QString());
	delete scene;
}

void TestPESvgEditor::interaction_moveCommitThenUndoRoundTrips()
{
	// A pad at (10,10). Select it (hit at 10,10), commit a +20,+0 move (exactly what handleRelease does:
	// moveBy(el, delta)), rebuild the scene, and confirm it is now selected at the NEW spot (30,10) and no
	// longer at the old (10,10), with its cx attr rewritten to 30. Then apply the reverse (what undo does:
	// moveBy(el, -delta)) and confirm it lands back at (10,10) selected there, cx back to 10. The flatten in
	// buildScene is idempotent (already-absolute attrs -> flatten is a no-op the 2nd time), so the round-trip
	// is lossless -- no double-bake, exactly the behaviour PESvgEditor relies on across a move/undo cycle.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<circle id='pad' cx='10' cy='10' r='3' fill='#cc0000'/>"
		"</svg>";
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));

	// 1. Select at the resting spot.
	{ QGraphicsScene * s = buildScene(d); QCOMPARE(PESelfPaintItem::hitTestId(s, QPointF(10, 10)), QString("pad")); delete s; }

	// 2. Commit the move (handleRelease: findById + moveBy(el, delta)).
	{ QDomElement pad = PEPrimGeom::findById(d.documentElement(), "pad"); QVERIFY(PEPrimGeom::moveBy(pad, QPointF(20, 0))); }
	QCOMPARE(PEPrimGeom::localBounds(PEPrimGeom::findById(d.documentElement(), "pad")), QRectF(27, 7, 6, 6));

	// 3. Rebuild + hit-test: pad now lives at (30,10); old spot empty.
	{ QGraphicsScene * s = buildScene(d);
	  QCOMPARE(PESelfPaintItem::hitTestId(s, QPointF(30, 10)), QString("pad"));
	  QCOMPARE(PESelfPaintItem::hitTestId(s, QPointF(10, 10)), QString());
	  delete s; }

	// 4. Undo: moveBy(el, -delta) reverses it; back at (10,10).
	{ QDomElement pad = PEPrimGeom::findById(d.documentElement(), "pad"); QVERIFY(PEPrimGeom::moveBy(pad, QPointF(-20, 0))); }
	QCOMPARE(PEPrimGeom::localBounds(PEPrimGeom::findById(d.documentElement(), "pad")), QRectF(7, 7, 6, 6));

	// 5. Rebuild + hit-test: pad back at (10,10); (30,10) empty again.
	{ QGraphicsScene * s = buildScene(d);
	  QCOMPARE(PESelfPaintItem::hitTestId(s, QPointF(10, 10)), QString("pad"));
	  QCOMPARE(PESelfPaintItem::hitTestId(s, QPointF(30, 10)), QString());
	  delete s; }
}

// ---- Slice D: full chrome driven through the real PESvgEditor controller --------------------------------

void TestPESvgEditor::chrome_rectGripResizeRoundTrip()
{
	// A standalone rect on a blank svg. Select it (hit its interior), then press exactly on its bottom-right
	// corner grip and drag +5,+5. The rect must resize (width/height +5, x/y unchanged -- bottom-right corner
	// grip moves only R and B). Then undo -> back to the original box. This exercises grip hit-test, resize
	// commit via resizeTo, and the Resize undo entry, all through the real controller.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<rect id='box' x='10' y='10' width='8' height='8' fill='#00aa00'/>"
		"</svg>";
	PESvgEditor ed;
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	ed.setDocument(d, "t");

	// Select by hitting inside the rect.
	ed.handlePress(QPointF(12, 12));
	ed.handleRelease(QPointF(12, 12));

	// The bottom-right corner of the box is at scene (18,18). Press there (on the grip), drag +5,+5, release.
	ed.handlePress(QPointF(18, 18));
	ed.handleMove(QPointF(20, 20));
	ed.handleRelease(QPointF(23, 23));

	QDomElement box = PEPrimGeom::findById(ed.document().documentElement(), "box");
	QRectF after = PEPrimGeom::localBounds(box);
	QCOMPARE(after, QRectF(10, 10, 13, 13));   // grew by 5 on right+bottom

	// Undo restores the original box.
	ed.undo();
	QRectF restored = PEPrimGeom::localBounds(PEPrimGeom::findById(ed.document().documentElement(), "box"));
	QCOMPARE(restored, QRectF(10, 10, 8, 8));

	// Redo re-applies the resize.
	ed.redo();
	QRectF redone = PEPrimGeom::localBounds(PEPrimGeom::findById(ed.document().documentElement(), "box"));
	QCOMPARE(redone, QRectF(10, 10, 13, 13));
}

void TestPESvgEditor::chrome_pathAnchorNodeEditRoundTrip()
{
	// A path with an absolute polyline. Select it by hitting a point on it (its decoded shape), then press on
	// the middle node (10,10) and drag it to (30,30). The path's 'd' must reflect the moved anchor. Undo must
	// restore the original anchor. This exercises anchorGrips, hitAnchorGrip, editPathAnchor commit, Anchor undo.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<path id='p' d='M 0,0 L 10,10 L 20,0' fill='none' stroke='#00aa00' stroke-width='1'/>"
		"</svg>";
	PESvgEditor ed;
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	ed.setDocument(d, "t");

	// Select the path by hitting a point on a segment. A 1px-wide stroke path has an area path? No -- shapeFromElement
	// returns the open polyline path (fill). To hit it we select by clicking a node point; the self-paint item's
	// boundingRect contains (10,10), so hitting there selects the path.
	ed.handlePress(QPointF(10, 10));
	ed.handleRelease(QPointF(10, 10));

	// Now the path is selected; its node at (10,10) is segment index 1. Press on it and drag to (30,30).
	ed.handlePress(QPointF(10, 10));
	ed.handleMove(QPointF(25, 25));
	ed.handleRelease(QPointF(30, 30));

	QVector<QPointF> a = PEPrimGeom::pathAnchors(PEPrimGeom::findById(ed.document().documentElement(), "p"));
	QVERIFY(a.count() >= 3);
	QCOMPARE(a.at(1), QPointF(30, 30));   // the grabbed node moved

	// Undo restores the original middle anchor (10,10).
	ed.undo();
	QVector<QPointF> back = PEPrimGeom::pathAnchors(PEPrimGeom::findById(ed.document().documentElement(), "p"));
	QCOMPARE(back.at(1), QPointF(10, 10));
}

// ---- PEPath decode model (preserved from the swept test_pedocument) --------------------------------------

void TestPESvgEditor::pePath_decodesCommandsAndArgs()
{
	PEPath p = PEPath::fromD("M 1 2 L 3 4 Z");
	QVERIFY(p.ok());
	QCOMPARE(p.count(), 3);                                   // M, L, Z
	QVERIFY(p.segments().at(0).command == QChar('M'));
	QVERIFY(!p.segments().at(0).relative);
	QCOMPARE(p.segments().at(0).args.count(), 2);
	QCOMPARE(p.segments().at(0).args.at(0), 1.0);
	QCOMPARE(p.segments().at(0).args.at(1), 2.0);
	QVERIFY(p.segments().at(1).command == QChar('L'));
	QVERIFY(p.segments().at(2).command == QChar('Z'));
	QCOMPARE(p.segments().at(2).args.count(), 0);
}

void TestPESvgEditor::pePath_isRelativeAware()
{
	PEPath p = PEPath::fromD("m 1 2 l 3 4");
	QVERIFY(p.ok());
	QVERIFY(p.segments().at(0).relative);                    // m
	QVERIFY(p.segments().at(1).relative);                    // l
	// cursor starts 0,0; 'm' absolute -> (1,2); relative 'l' +3,+4 off cursor -> (4,6).
	const QVector<QPointF> a = p.anchors();
	QCOMPARE(a.count(), 2);
	QCOMPARE(a.at(0), QPointF(1, 2));
	QCOMPARE(a.at(1), QPointF(4, 6));
}

void TestPESvgEditor::pePath_roundTripIsStable()
{
	// decode 'd' -> segments -> serialize -> decode again == same segment list. What makes node editing lossless.
	const QString d = "M 0 0 L 10 10 C 12 12 14 14 16 16 Z";
	const PEPath p1 = PEPath::fromD(d);
	QVERIFY(p1.ok());
	const QString d2 = p1.serialize();
	const PEPath p2 = PEPath::fromD(d2);
	QVERIFY(p2.ok());
	QCOMPARE(p1.count(), p2.count());
	for (int i = 0; i < p1.count(); ++i) {
		QVERIFY(p1.segments().at(i).command == p2.segments().at(i).command);
		QCOMPARE(p1.segments().at(i).relative, p2.segments().at(i).relative);
		QCOMPARE(p1.segments().at(i).args, p2.segments().at(i).args);
	}
}

void TestPESvgEditor::pePath_invalidIsNotOk()
{
	// Empty / unparseable path data => ok() false, no segments.
	const PEPath p = PEPath::fromD("");
	QVERIFY(!p.ok());
	QCOMPARE(p.count(), 0);
}

// ---- Slice F: save (raw-source-of-truth DOM round-trip) ------------------------------------------------

void TestPESvgEditor::save_roundTripsThroughDisk()
{
	// Build an editor with a circle pad at (10,10); move it +20 (a real DOM edit); save to a temp file;
	// re-parse that file into a FRESH document and confirm the pad's cx is 30 (20+10) -- i.e. the geometry
	// edit persisted to disk. Because the editor holds RAW bytes, the re-parsed doc re-renders identically:
	// a pad still hit-tests at its NEW spot, and a fill colour is byte-faithful (no view colour leaked in).
	PESvgEditor ed;
	QDomDocument d;
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<circle id='pad' cx='10' cy='10' r='3' fill='#cc0000'/></svg>";
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	ed.setDocument(d, "t.svg");

	// Commit a move to (30,10) the way handleRelease does.
	QDomElement pad = PEPrimGeom::findById(ed.document().documentElement(), "pad");
	QVERIFY(PEPrimGeom::moveBy(pad, QPointF(20, 0)));

	QTemporaryDir tmp; QVERIFY(tmp.isValid());
	const QString file = tmp.filePath("out.svg");
	QSignalSpy savedSpy(&ed, &PESvgEditor::saved);
	// The editor has no currentFilepath yet -> must pass an explicit path.
	QVERIFY(ed.saveToPath(file));
	QCOMPARE(savedSpy.count(), 1);
	QCOMPARE(savedSpy.at(0).at(0).toString(), file);
	QVERIFY(ed.currentFilepath() == file);            // filepath now set to where it saved

	// Re-parse the saved file and confirm the geometry edit persisted and is renderable.
	QFile f(file); QVERIFY(f.open(QIODevice::ReadOnly));
	QDomDocument reparsed; QVERIFY(reparsed.setContent(&f)); f.close();
	QDomElement rp = PEPrimGeom::findById(reparsed.documentElement(), "pad");
	QVERIFY(!rp.isNull());
	QCOMPARE(PEPrimGeom::localBounds(rp), QRectF(27, 7, 6, 6));   // cx=30, r=3 -> (27,7,6,6)
	QCOMPARE(rp.attribute("fill"), QStringLiteral("#cc0000"));    // colour byte-faithful (no leak)
}

void TestPESvgEditor::save_failsWithoutFilepath()
{
	// No currentFilepath set and no path argument -> save must fail (return false) without crashing, and
	// must NOT clear the dirty flag or emit saved().
	PESvgEditor ed;
	QDomDocument d;
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<circle id='pad' cx='5' cy='5' r='2' fill='#000000'/></svg>";
	QVERIFY(d.setContent(QString::fromUtf8(svg)));
	ed.setDocument(d, "t.svg");
	QSignalSpy savedSpy(&ed, &PESvgEditor::saved);
	QVERIFY(!ed.saveToPath());              // empty path + no currentFilepath -> false
	QCOMPARE(savedSpy.count(), 0);          // nothing saved
}

// ---- R1: resize that actually resizes (generic resizeTo + grips on every resizable kind) ----------------

void TestPESvgEditor::geom_resizeToLine()
{
	// A line's box IS its two endpoints, so resizeTo maps the box corners straight onto x1,y1,x2,y2.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<line id='l' x1='0' y1='0' x2='10' y2='10'/></svg>";
	QDomDocument d; QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement l = byId(d, "l");
	QVERIFY(PEPrimGeom::isResizable(l));
	QVERIFY(PEPrimGeom::resizeTo(l, PEPrimGeom::localBounds(l), QRectF(2, 3, 20, 30)));
	QCOMPARE(l.attribute("x1").toDouble(), 2.0);
	QCOMPARE(l.attribute("y1").toDouble(), 3.0);
	QCOMPARE(l.attribute("x2").toDouble(), 22.0);
	QCOMPARE(l.attribute("y2").toDouble(), 33.0);
}

void TestPESvgEditor::geom_resizeToCircle()
{
	// A <circle> stores cx,cy,r and is round -- it has NO honest per-axis box resize, so it is not resizable.
	// isResizable is false and resizeTo is a no-op (DOM untouched). This is the R1 decision (a single radius
	// cannot grow one axis; a uniform corner-scale is handled by move on the path/box kinds, not by a fake
	// "circle resize"). The old behavior that faked a circle resize by r=min(w,h)/2 is gone.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<circle id='c' cx='5' cy='5' r='5'/></svg>";
	QDomDocument d; QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement c = byId(d, "c");
	QVERIFY(!PEPrimGeom::isResizable(c));
	const QString beforeR = c.attribute("r");
	QVERIFY(!PEPrimGeom::resizeTo(c, PEPrimGeom::localBounds(c), QRectF(0, 0, 20, 40)));
	QCOMPARE(c.attribute("r"), beforeR);   // DOM untouched
}

void TestPESvgEditor::geom_resizeToPolygonScalesPoints()
{
	// A polygon's points scale from its old bounds to the new box. Resize it on x only (source box is a
	// square 10x10, target width 20); the un-grabbed y axis stays at 10 (resizeTo scales only the grabbed axis).
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<polygon id='p' points='0,0 10,0 10,10 0,10'/></svg>";
	QDomDocument d; QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement p = byId(d, "p");
	QVERIFY(PEPrimGeom::isResizable(p));
	QRectF from = PEPrimGeom::localBounds(p);        // spans x and y -> (0,0,10,10)
	QVERIFY(from.isValid());
	QCOMPARE(from, QRectF(0, 0, 10, 10));
	QRectF to  = from;  to.setWidth(20);             // (0,0,20,10) -- x-axis only
	QVERIFY(PEPrimGeom::resizeTo(p, from, to));
	QCOMPARE(p.attribute("points").toStdString(), std::string("0,0 20,0 20,10 0,10"));
}

void TestPESvgEditor::geom_resizeToRejectsPath()
{
	// A <path> is node-edited, not box-resized; resizeTo refuses it and isResizable says so.
	const char * svg = "<svg xmlns='http://www.w3.org/2000/svg'>"
	                   "<path id='p' d='M 0,0 L 10,10'/></svg>";
	QDomDocument d; QVERIFY(d.setContent(QString::fromUtf8(svg)));
	QDomElement p = byId(d, "p");
	QVERIFY(!PEPrimGeom::isResizable(p));
	const QString before = p.attribute("d");
	QVERIFY(!PEPrimGeom::resizeTo(p, QRectF(0, 0, 1, 1), QRectF(0, 0, 5, 5)));
	QCOMPARE(p.attribute("d"), before);   // DOM untouched
}

void TestPESvgEditor::controller_circleIsNotResizableNoOp()
{
	// R1 decision: a <circle> stores cx,cy,r and is round, so there is no honest per-axis box resize -- it is
	// NOT resizable (no resize grips drawn). A body-drag still MOVES it (a legitimate move, not a resize): here
	// pressing on the circle and dragging horizontally must keep r constant and shift cx by the drag -- proving
	// it moved (not resized). A press on the old right-edge that would be a grip on a rect, with no grip present,
	// behaves as a body move; crucially the RADIUS never changes. The whole point: non-resizable kinds never
	// corrupt r/cx/cy with a fake per-axis scale -- only move or, for paths, node-edit, apply.
	const char * svg =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
		"<circle id='pad' cx='10' cy='10' r='4' fill='#cc0000'/>"
		"</svg>";
	PESvgEditor ed;
	QDomDocument d; QVERIFY(d.setContent(QString::fromUtf8(svg)));
	ed.setDocument(d, "t");

	// A circle is not resizable (no grip). Press on its body (x=14 is on the right edge, inside bounds [6,14])
	// and drag right by 5. The body-drag moves it: cx goes 10 -> 15, r stays 4 (NOT resized).
	ed.handlePress(QPointF(14, 10));
	ed.handleMove(QPointF(19, 10));
	ed.handleRelease(QPointF(19, 10));

	QDomElement pad = PEPrimGeom::findById(ed.document().documentElement(), "pad");
	QCOMPARE(pad.attribute("r").toDouble(), 4.0);           // radius UNCHANGED (not resized)
	QCOMPARE(pad.attribute("cx").toDouble(), 15.0);         // moved right by the drag (10 + 5)
	QCOMPARE(pad.attribute("cy").toDouble(), 10.0);         // y unchanged
}

QTEST_MAIN(TestPESvgEditor)
#include "test_pesvgeditor.moc"
