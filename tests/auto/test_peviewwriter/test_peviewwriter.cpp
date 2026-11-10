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

// Golden tests for the sole-renderer Parts Editor DOM-writer (docs/design/pe-integration-plan.md §4):
// PEViewWriter serializes the editor's QDomDocument straight to a view .svg with the app's XML
// conventions (svgNSOnly namespace normalization, preserved attribute order) and writes it atomically.
//
// These exercise persistence without a QGuiApplication or the fritzing-parts submodule: a tiny synthetic
// document stands in for a part view, and the assertions are on the exact bytes produced and the file
// that lands on disk -- the same golden style test_pesvgeditor uses for the colourise/geometry engine.
//
// NOTE (AGENTS.md 5): CONFIG += testcase requires the Q_OBJECT test class to live IN this .cpp so
// qmake's in-source moc rule keys on it; keep the class here, not in a sibling header.

#include <QtTest>
#include <QDomDocument>
#include <QTemporaryDir>
#include <QFile>

#include "peviewwriter.h"

// A minimal view graphic: one copper group with a pad whose colour is an arbitrary author colour. The
// writer must preserve element/attribute structure and order byte-for-byte, only normalizing the
// namespace (svgNSOnly), and must NOT recolour anything -- colourisation is a view-time transform and is
// never part of the persisted svg.
static const char * kSynthView =
	"<svg xmlns='http://www.w3.org/2000/svg' version='1.2' viewBox='0 0 10 10'>"
	"<g id='copper0'><g id='copper1'>"
	"<circle id='connector0pad' cx='2' cy='2' r='0.5' stroke='#abcdef' fill='none'/>"
	"</g></g>"
	"</svg>";

class TestPEViewWriter : public QObject
{
	Q_OBJECT

private slots:
	// svgNSOnly convention: a single canonical xmlns on the root, no inherited xmlns:svg alias.
	void normalizesNamespace();
	// Attribute name/value and element order survive the round-trip through normalizedSvg.
	void preservesAttributesAndOrder();
	// Colour is data, not the writer's concern: the author's #abcdef must be untouched on disk.
	void doesNotRecolour();
	// writeViewSvg creates parent dirs, writes the exact normalizedSvg bytes atomically, reads back equal.
	void writesAtomicallyAndReadsBack();
	// Honest no-op: empty path / null doc touch nothing and return false.
	void rejectsEmptyPathAndNullDoc();
	// Idempotence: re-parsing what we wrote and re-normalizing yields identical bytes.
	void reparseIsStable();
};

void TestPEViewWriter::normalizesNamespace()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthView)));
	const QString out = PEViewWriter::normalizedSvg(d);
	// Canonical svg namespace present; the aliasing/secondary xmlns:svg form is gone.
	QVERIFY2(out.contains("xmlns=\"http://www.w3.org/2000/svg\""), qPrintable(out));
	QVERIFY2(!out.contains("xmlns:svg"), qPrintable(out));
}

void TestPEViewWriter::preservesAttributesAndOrder()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthView)));
	const QString out = PEViewWriter::normalizedSvg(d);

	// Every authored attribute is present with its value, in document order (copper0 outer, copper1
	// inner, pad last). Indexing proves the order without depending on whitespace.
	const int iCopper0 = out.indexOf("copper0");
	const int iCopper1 = out.indexOf("copper1");
	const int iPad     = out.indexOf("connector0pad");
	QVERIFY2(iCopper0 >= 0 && iCopper1 >= 0 && iPad >= 0, qPrintable(out));
	QVERIFY2(iCopper0 < iCopper1 && iCopper1 < iPad, qPrintable(out));

	QVERIFY2(out.contains("cx=\"2\""), qPrintable(out));
	QVERIFY(out.contains("cy=\"2\""));
	QVERIFY(out.contains("r=\"0.5\""));
	QVERIFY(out.contains("fill=\"none\""));
}

void TestPEViewWriter::doesNotRecolour()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthView)));
	const QString out = PEViewWriter::normalizedSvg(d);
	// The pad's authored stroke survives verbatim -- proof the writer is a faithful serializer, not a
	// recolour pass. (The copper gold/orange colours must NOT appear here.)
	QVERIFY2(out.contains("stroke=\"#abcdef\""), qPrintable(out));
	QVERIFY(!out.contains("#fdde68"));
	QVERIFY(!out.contains("#f9a435"));
}

void TestPEViewWriter::writesAtomicallyAndReadsBack()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.path() + "/sub/pcb/view.svg";   // sub/ does not exist yet

	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthView)));
	QVERIFY(PEViewWriter::writeViewSvg(d, path));                 // creates sub/pcb then writes

	QFile f(path);
	QVERIFY(f.open(QIODevice::ReadOnly));
	const QByteArray raw = f.readAll();
	f.close();

	// The bytes on disk equal normalizedSvg exactly (no BOM, no trailing churn).
	QCOMPARE(QString::fromUtf8(raw), PEViewWriter::normalizedSvg(d));

	// And they re-parse into a tree that still carries the pad by id.
	QDomDocument back;
	QVERIFY(back.setContent(QString::fromUtf8(raw)));
	QVERIFY(!back.elementsByTagName("circle").isEmpty());
	QVERIFY(back.elementsByTagName("circle").at(0).toElement().attribute("id") == "connector0pad");
}

void TestPEViewWriter::rejectsEmptyPathAndNullDoc()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.path() + "/none.svg";

	QDomDocument good;
	QVERIFY(good.setContent(QString::fromUtf8(kSynthView)));

	// Empty path: refuse, create nothing.
	QVERIFY(!PEViewWriter::writeViewSvg(good, QString()));
	QVERIFY(!QFile::exists(path));

	// Null doc against a valid path: refuse, create nothing.
	QDomDocument empty;
	QVERIFY(!PEViewWriter::writeViewSvg(empty, path));
	QVERIFY(!QFile::exists(path));
}

void TestPEViewWriter::reparseIsStable()
{
	QDomDocument d;
	QVERIFY(d.setContent(QString::fromUtf8(kSynthView)));
	const QString once = PEViewWriter::normalizedSvg(d);

	// Re-parse the normalized bytes and normalize again: byte-identical. This is what guarantees a
	// save/reopen/save cycle does not drift the file on disk.
	QDomDocument d2;
	QVERIFY(d2.setContent(once));
	QCOMPARE(PEViewWriter::normalizedSvg(d2), once);
}

QTEST_GUILESS_MAIN(TestPEViewWriter)
#include "test_peviewwriter.moc"

