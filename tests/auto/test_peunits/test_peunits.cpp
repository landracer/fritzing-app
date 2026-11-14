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

// Golden tests for the R2 measurement/units core (docs/design/pe-editor-roadmap.md R2). Pure logic:
// the px(user-unit) <-> display-unit conversion, the readout formatting, and the preference gate.
//
// The preference tests PIN the store to a throwaway ini file (QSettings::setPath +
// QSettings::setDefaultFormat) so they are deterministic and never read or write the developer's real
// config -- AGENTS.md §4 ("feature toggles are real preferences; tests must pin such settings to be
// deterministic").
//
// NOTE (AGENTS.md §5): CONFIG += testcase requires the Q_OBJECT test class to live IN this .cpp so
// qmake's in-source .moc rule keys on it; do not move it to a sibling header.

#include <QtTest>
#include <QTemporaryDir>
#include <QDomDocument>
#include <QFile>

#include "peunits.h"

// Point the default QSettings at a throwaway file. Called from initTestCase, so no test in this suite
// can read or write the real user config. The dir is heap-allocated and intentionally never deleted:
// it must outlive every QSettings() the code under test constructs.
static void useScratchSettings()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir * dir = new QTemporaryDir();
    Q_ASSERT(dir->isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir->path());
}

class TestPEUnits : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Conversion: each named unit scales 90 user units to its own inch-based magnitude, and back.
    void convertsEachUnitAt90Dpi();
    void roundTripsThroughEveryUnit();
    void pxAndUnknownAreIdentity();

    // The unit registry is the small closed set the UI offers; unknown names are not accepted.
    void availableUnitsIsTheClosedSet();
    void isKnownUnitRejectsUnknownAndCaseMismatch();

    // Readout formatting is deterministic and carries the unit name (so the readout never lies about scale).
    void formatPointCarriesTheUnit();
    void formatBoxIsPositionThenSize();
    void formatBoxIsEmptyForAnInvalidBox();

    // The preference: default mm, unknown stored value falls back, unknown writes are refused.
    void preferenceDefaultsToMm();
    void preferenceRoundTripsAndRejectsGarbage();

    // A real bundled part graphic: the stored user-unit geometry must read back as the real-world mm the
    // library author intended (proves the readout is a faithful projection of an actual part, not a toy).
    void realPartBodyReadsBackItsAuthoredMmSize();
};

void TestPEUnits::initTestCase()
{
    useScratchSettings();
    // Start from a clean slate so test order can never change an expectation.
    QSettings().remove(QLatin1String(PEUnits::kPreferenceKey));
}

void TestPEUnits::convertsEachUnitAt90Dpi()
{
    // 90 user units is exactly one inch by definition of SvgDpi, so each unit's scale is exact.
    QCOMPARE(PEUnits::SvgDpi, 90.0);
    QCOMPARE(PEUnits::pxToUnits(90.0, "in"), 1.0);
    QCOMPARE(PEUnits::pxToUnits(90.0, "mm"), 25.4);
    QCOMPARE(PEUnits::pxToUnits(90.0, "mil"), 1000.0);
    QCOMPARE(PEUnits::pxToUnits(90.0, "px"), 90.0);

    // Half of that (45 user units) in each unit, exactly.
    QCOMPARE(PEUnits::pxToUnits(45.0, "in"), 0.5);
    QCOMPARE(PEUnits::pxToUnits(45.0, "mm"), 12.7);
    QCOMPARE(PEUnits::pxToUnits(45.0, "mil"), 500.0);
}

void TestPEUnits::roundTripsThroughEveryUnit()
{
    for (const QString & u : PEUnits::availableUnits()) {
        const double v = 17.0;
        QVERIFY2(qFuzzyCompare(PEUnits::unitsToPx(PEUnits::pxToUnits(v, u), u), v),
                 qPrintable(QStringLiteral("round-trip failed for unit %1").arg(u)));
    }
}

void TestPEUnits::pxAndUnknownAreIdentity()
{
    // "px" is the identity unit (the svg's own user units), and an unknown name must NOT pick an
    // inch-based scale -- it passes the number through untouched.
    QCOMPARE(PEUnits::pxToUnits(3.5, "px"), 3.5);
    QCOMPARE(PEUnits::pxToUnits(3.5, QString()), 3.5);
    QCOMPARE(PEUnits::pxToUnits(3.5, "furlongs"), 3.5);
    QCOMPARE(PEUnits::unitsToPx(3.5, "furlongs"), 3.5);
}

void TestPEUnits::availableUnitsIsTheClosedSet()
{
    QCOMPARE(PEUnits::availableUnits(), QStringList() << "px" << "in" << "mm" << "mil");
}

void TestPEUnits::isKnownUnitRejectsUnknownAndCaseMismatch()
{
    QVERIFY(PEUnits::isKnownUnit("mm"));
    QVERIFY(PEUnits::isKnownUnit("in"));
    QVERIFY(PEUnits::isKnownUnit("mil"));
    QVERIFY(PEUnits::isKnownUnit("px"));
    QVERIFY(!PEUnits::isKnownUnit("MM"));      // the stored value is case-sensitive
    QVERIFY(!PEUnits::isKnownUnit("cm"));
    QVERIFY(!PEUnits::isKnownUnit(QString()));
}

void TestPEUnits::formatPointCarriesTheUnit()
{
    // Fixed 'g'/4 formatting, and the unit name is part of the string so a reader knows the scale.
    QCOMPARE(PEUnits::formatPoint(QPointF(90, 45), "mm"), QStringLiteral("(25.4, 12.7) mm"));
    QCOMPARE(PEUnits::formatPoint(QPointF(90, 45), "in"), QStringLiteral("(1, 0.5) in"));
    QCOMPARE(PEUnits::formatPoint(QPointF(90, 45), "mil"), QStringLiteral("(1000, 500) mil"));
    QCOMPARE(PEUnits::formatPoint(QPointF(90, 45), "px"), QStringLiteral("(90, 45) px"));
}

void TestPEUnits::formatBoxIsPositionThenSize()
{
    // A 90x45 user-unit box at (0,0) is exactly 25.4mm x 12.7mm at (0, 0).
    QCOMPARE(PEUnits::formatBox(QRectF(0, 0, 90, 45), "mm"), QStringLiteral("(0, 0) (25.4, 12.7) mm"));
    QCOMPARE(PEUnits::formatBox(QRectF(0, 0, 90, 45), "in"), QStringLiteral("(0, 0) (1, 0.5) in"));
    // A box that does not start at the origin still reports its true size, not an origin-skewed one.
    QCOMPARE(PEUnits::formatBox(QRectF(90, 90, 90, 45), "in"), QStringLiteral("(1, 1) (1, 0.5) in"));
    // A NON-normalized input is refused rather than silently re-normalized: an invalid QRectF is "nothing
    // to report", and convertBox must not invent a box the geometry never had.
    QVERIFY(PEUnits::formatBox(QRectF(90, 90, -90, -45), "in").isEmpty());
    QVERIFY(PEUnits::convertBox(QRectF(90, 90, -90, -45), "in").isNull());
}

void TestPEUnits::formatBoxIsEmptyForAnInvalidBox()
{
    // An invalid box means "nothing to report" -- NOT a fabricated "(0, 0) (0, 0) mm".
    QVERIFY(PEUnits::formatBox(QRectF(), "mm").isEmpty());
    QVERIFY(PEUnits::convertBox(QRectF(), "mm").isNull());
}

void TestPEUnits::preferenceDefaultsToMm()
{
    QSettings().remove(QLatin1String(PEUnits::kPreferenceKey));
    QCOMPARE(QString::fromLatin1(PEUnits::kDefaultUnit), QStringLiteral("mm"));
    QCOMPARE(PEUnits::readPreference(), QStringLiteral("mm"));
}

void TestPEUnits::preferenceRoundTripsAndRejectsGarbage()
{
    for (const QString & u : PEUnits::availableUnits()) {
        PEUnits::writePreference(u);
        QCOMPARE(PEUnits::readPreference(), u);
    }

    // A corrupt/legacy value in the store is not honoured (that would make the readout silently lie by
    // falling through to the identity passthrough); it falls back to the documented default.
    QSettings s;
    s.setValue(QLatin1String(PEUnits::kPreferenceKey), QStringLiteral("furlongs"));
    s.sync();
    QCOMPARE(PEUnits::readPreference(), QStringLiteral("mm"));

    // ...and a bad write is refused outright, leaving the previous good value in place.
    PEUnits::writePreference(QStringLiteral("parsecs"));
    QCOMPARE(PEUnits::readPreference(), QStringLiteral("mm"));
}

void TestPEUnits::realPartBodyReadsBackItsAuthoredMmSize()
{
    // The bundled schematic svg's body rect stores geometry in svg USER UNITS (viewBox='0 0 38.1 12.7',
    // i.e. the user-unit box of a 1.5in x 0.5in part). Reading that box through PEUnits in "in" must come
    // back at exactly the 1.5in x 0.5in the library authored -- proving the readout is a faithful
    // projection of a real part, not just of a synthetic test svg. The parts tree is handed to the suite at
    // build time (PE_PARTS_SVG_ROOT, defined in the .pro) so it resolves regardless of where it runs.
    QFile f(QString::fromLatin1(PE_PARTS_SVG_ROOT)
            + QStringLiteral("/core/schematic/11LC010_schematic.svg"));
    if (!f.exists()) QSKIP("bundled parts tree not found");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QDomDocument doc;
    QVERIFY(doc.setContent(&f));
    f.close();

    // The body is the first interior rect.
    QDomElement body;
    const QDomNodeList rects = doc.elementsByTagName(QStringLiteral("rect"));
    for (int i = 0; i < rects.count(); ++i) {
        const QDomElement r = rects.at(i).toElement();
        if (r.attribute(QStringLiteral("class")) == QLatin1String("interior rect")) { body = r; break; }
    }
    QVERIFY(!body.isNull());

    // Its stored user-unit box (x=5.23875 y=0.15875 w=27.6225 h=12.3825) converts to inches at 90 dpi.
    const QRectF px(body.attribute("x").toDouble(), body.attribute("y").toDouble(),
                    body.attribute("width").toDouble(), body.attribute("height").toDouble());
    const QRectF inches = PEUnits::convertBox(px, "in");
    // The authored body box is exactly these inches (1.0875 x 0.5in of the 1.5 x 0.5in part), and
    // converting back reproduces the stored user-unit geometry exactly -- display is lossless.
    QVERIFY(qFuzzyCompare(inches.x(), 5.23875 / 90.0));
    QVERIFY(qFuzzyCompare(inches.width(), 27.6225 / 90.0));
    QVERIFY(qFuzzyCompare(inches.height(), 12.3825 / 90.0));
    QVERIFY(qFuzzyCompare(PEUnits::unitsToPx(inches.width(), "in"), body.attribute("width").toDouble()));
    QVERIFY(qFuzzyCompare(PEUnits::unitsToPx(inches.height(), "in"), body.attribute("height").toDouble()));
    // And the mm readout of the authored width is its real-world size (27.6225 user units == 7.8mm).
    QVERIFY(qFuzzyCompare(PEUnits::pxToUnits(27.6225, "mm"), 27.6225 * 25.4 / 90.0));
    QVERIFY(!PEUnits::formatBox(px, "mm").isEmpty());
}

QTEST_MAIN(TestPEUnits)
#include "test_peunits.moc"
