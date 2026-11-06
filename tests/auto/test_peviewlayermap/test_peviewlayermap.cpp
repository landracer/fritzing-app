#include "peviewlayermap.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QString>

/**
 * Tests for PEViewLayerMap -- which <g> a newly-created graphic must be inserted into, per view.
 *
 * The class under test lives in src/partseditor/peviewlayermap.{h,cpp}. The test class itself is
 * declared HERE rather than in a sibling header, because QtTest's "testcase" config generates the
 * in-source test_<name>.moc by moc'ing the test source; a Q_OBJECT that only exists in a separate
 * header is not scanned, and the aggregate build then dies with
 * "No rule to make target 'test_peviewlayermap.moc'". (test_panelizer does the same thing.)
 */
class TestPEViewLayerMap : public QObject {
    Q_OBJECT

private slots:
    void pcbGraphicsGoToSilkscreenNotRoot();
    void pcbCopperGoesToOutermostCopperGroup();
    void nestedCopperPairIsDetected();
    void schematicGraphicsGoToSchematicGroup();
    void unknownStructureIsRefusedNotRootAppended();
    void missingLayerIsNotInvented();
    void realPcbPartFromDisk();
    // Regression: the M1 layer-map rollout turned 'Add graphic' into a silent no-op whenever the
    // live map did not currently hold an entry for the view, because addPrimitive refused instead
    // of falling back. These two pin the map contract that fallback relies on.
    void unresolvedViewIsRecoveredByReInspectingTheLiveDocument();
    void unrecognisableStructureStillHasNoTarget();
};

// Build an <svg> document from a fragment and hand back its root element.
static QDomElement parseRoot(QDomDocument & doc, const QString & body) {
    const QString xml = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' version='1.2'>%1</svg>").arg(body);
    QDomDocument::ParseResult r = doc.setContent(xml, QDomDocument::ParseOption::UseNamespaceProcessing);
    Q_ASSERT(static_cast<bool>(r));
    return doc.documentElement();
}

// Resolve a parent the way a create would, append a rect into it, and report which group won.
static QString resolvedParentId(QDomDocument & doc, ViewLayer::ViewID viewID,
                                const PEViewLayerMap & map, bool isCopper = false) {
    QDomElement root = doc.documentElement();
    QDomElement p = map.resolveParent(root, viewID, isCopper);
    if (p.isNull()) return QStringLiteral("<refused>");
    QDomElement rect = doc.createElement(QStringLiteral("rect"));
    rect.setAttribute(QStringLiteral("id"), QStringLiteral("newshape"));
    p.appendChild(rect);
    return p.attribute(QStringLiteral("id"));
}

/**
 * The reported bug, pinned: shapes added to the PCB view were appended to the <svg> root, outside
 * every layer group, producing artifacts against the copper/silkscreen structure. They must land
 * inside the silkscreen group.
 */
void TestPEViewLayerMap::pcbGraphicsGoToSilkscreenNotRoot() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc,
        "<g id='copper1'><g id='copper0'/></g>"
        "<g id='silkscreen'/>");

    PEViewLayerMap map;
    map.inspect(root);

    QVERIFY(map.contains(ViewLayer::PCBView));
    QCOMPARE(resolvedParentId(doc, ViewLayer::PCBView, map), QStringLiteral("silkscreen"));

    // Prove the rect is genuinely nested inside silkscreen, not a sibling at the root.
    QDomElement added = root.firstChildElement(QStringLiteral("g"))
                            .nextSiblingElement(QStringLiteral("g"))
                            .firstChildElement(QStringLiteral("rect"));
    QVERIFY(!added.isNull());
    QCOMPARE(added.parentNode().toElement().attribute(QStringLiteral("id")),
             QStringLiteral("silkscreen"));
}

/** Copper graphics must go to the OUTERMOST copper group so a nested copper0 keeps its place. */
void TestPEViewLayerMap::pcbCopperGoesToOutermostCopperGroup() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc,
        "<g id='copper1'><g id='copper0'/></g>"
        "<g id='silkscreen'/>");

    PEViewLayerMap map;
    map.inspect(root);

    QCOMPARE(resolvedParentId(doc, ViewLayer::PCBView, map, true), QStringLiteral("copper1"));
}

/** The historically-mishandled part shape must be flagged so callers can warn. */
void TestPEViewLayerMap::nestedCopperPairIsDetected() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc, "<g id='copper1'><g id='copper0'/></g>");

    PEViewLayerMap map;
    map.inspect(root);
    QVERIFY(map.targetsFor(ViewLayer::PCBView).hasCopperPair);

    // A single copper layer is the easy case and must NOT be flagged.
    QDomDocument doc2;
    QDomElement root2 = parseRoot(doc2, "<g id='copper1'/>");
    PEViewLayerMap map2;
    map2.inspect(root2);
    QVERIFY(!map2.targetsFor(ViewLayer::PCBView).hasCopperPair);
}

void TestPEViewLayerMap::schematicGraphicsGoToSchematicGroup() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc, "<g id='schematic'><rect id='body'/></g>");

    PEViewLayerMap map;
    map.inspect(root);
    QCOMPARE(resolvedParentId(doc, ViewLayer::SchematicView, map), QStringLiteral("schematic"));
}

/**
 * The behaviour change that makes corruption impossible: an unrecognised structure is REFUSED.
 * The old code appended to the root unconditionally, which is what corrupted layered parts.
 */
void TestPEViewLayerMap::unknownStructureIsRefusedNotRootAppended() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc, "<g id='somethingElse'><rect id='body'/></g>");

    PEViewLayerMap map;
    map.inspect(root);
    QVERIFY(!map.contains(ViewLayer::IconView));
    QVERIFY(map.resolveParent(root, ViewLayer::IconView, false).isNull());

    // A view never inspected at all is likewise refused.
    QDomDocument doc2;
    QDomElement root2 = parseRoot(doc2, "<g id='module'/>");
    PEViewLayerMap map2;
    map2.inspect(root2);
    QVERIFY(map2.contains(ViewLayer::BreadboardView));
    QVERIFY(map2.resolveParent(root2, ViewLayer::PCBView, false).isNull());
}

/** A part with no silkscreen group must not have graphics routed to one that does not exist. */
void TestPEViewLayerMap::missingLayerIsNotInvented() {
    QDomDocument doc;
    QDomElement root = parseRoot(doc, "<g id='copper1'><g id='copper0'/></g>");

    PEViewLayerMap map;
    map.inspect(root);

    // No silkscreen present, so the default falls back to the outermost copper group ...
    QCOMPARE(resolvedParentId(doc, ViewLayer::PCBView, map), QStringLiteral("copper1"));
    // ... and the recorded layer list must not claim a silkscreen exists.
    QVERIFY(!map.layerIdsFor(ViewLayer::PCBView).contains(QStringLiteral("silkscreen")));
}

/**
 * Ground truth against a real shipped part rather than a hand-written fixture: the PCB SVG in
 * parts/svg/core/pcb nests copper0 inside copper1, which is exactly the form the editor historically
 * mangled. Skipped if the parts submodule is not checked out.
 */
void TestPEViewLayerMap::realPcbPartFromDisk() {
    QString dir = QFINDTESTDATA(QStringLiteral("../../../parts/svg/core/pcb"));
    if (dir.isEmpty()) dir = QFINDTESTDATA(QStringLiteral("parts/svg/core/pcb"));
    const QString path = dir + QStringLiteral("/sparkfun-electromechanical_buzzer-cmt1102_pcb.svg");
    QFileInfo fi(path);
    if (!fi.exists()) QSKIP("parts submodule not checked out");

    QFile f(fi.absoluteFilePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QDomDocument doc;
    QDomDocument::ParseResult pr = doc.setContent(&f, QDomDocument::ParseOption::UseNamespaceProcessing);
    QVERIFY2(static_cast<bool>(pr), qPrintable(pr.errorMessage));
    f.close();

    QDomElement root = doc.documentElement();
    PEViewLayerMap map;
    map.inspect(root);

    QVERIFY(map.contains(ViewLayer::PCBView));
    QVERIFY(map.targetsFor(ViewLayer::PCBView).hasCopperPair);

    // Resolved parents must be real groups in the document, never the svg root.
    QDomElement silk = map.resolveParent(root, ViewLayer::PCBView, false);
    QDomElement cop  = map.resolveParent(root, ViewLayer::PCBView, true);
    QVERIFY(!silk.isNull());
    QVERIFY(!cop.isNull());
    QCOMPARE(cop.attribute(QStringLiteral("id")), QStringLiteral("copper1"));
    QVERIFY(silk.attribute(QStringLiteral("id")) != QStringLiteral("svg"));
    QVERIFY(silk != root);
}


// addPrimitive() resolves the parent against the document it is about to edit, and re-inspects that
// same document when the live map has nothing for the view. This is the exact sequence that used to
// produce "click Add rect, nothing happens": an empty/stale map plus a refusal. The document here is
// deliberately inspected only AFTER the first resolve, mirroring the stale-map state seen in the field.
void TestPEViewLayerMap::unresolvedViewIsRecoveredByReInspectingTheLiveDocument()
{
    QDomDocument doc;
    QDomElement root = parseRoot(doc, QStringLiteral(
        "<g id='copper0'><g id='copper1'/></g>"
        "<g id='silkscreen'/>"));

    PEViewLayerMap stale;                       // never inspected: the failure condition
    QVERIFY(stale.isEmpty());
    QVERIFY(stale.resolveParent(root, ViewLayer::PCBView).isNull());

    stale.inspect(root);                        // the recovery step addPrimitive now performs
    QDomElement recovered = stale.resolveParent(root, ViewLayer::PCBView);
    QVERIFY2(!recovered.isNull(),
             "re-inspecting the live document must resolve a parent, so a stale map cannot swallow the edit");
    QCOMPARE(recovered.attribute(QStringLiteral("id")), QStringLiteral("silkscreen"));
}

// The fallback still has to be distinguishable from a real hit: for a structure with no layer group at
// all, resolveParent must stay null so the caller logs the root-append rather than believing it routed.
void TestPEViewLayerMap::unrecognisableStructureStillHasNoTarget()
{
    QDomDocument doc;
    QDomElement root = parseRoot(doc, QStringLiteral(
        "<g id='Layer_1'><rect id='r' x='0' y='0' width='1' height='1'/></g>"));

    PEViewLayerMap map;
    map.inspect(root);
    QVERIFY(!map.contains(ViewLayer::PCBView));
    QVERIFY(map.resolveParent(root, ViewLayer::PCBView).isNull());
}

QTEST_MAIN(TestPEViewLayerMap)
#include "test_peviewlayermap.moc"
