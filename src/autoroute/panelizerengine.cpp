/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2026 Fritzing

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

#include "panelizerengine.h"
#include "panelizerseparators.h"
#include "../svg/fabexporter.h"

// Final destination: src/autoroute/panelizerengine.cpp
// Staged in: src/panelizer/panelizerengine.cpp

#include "../debugdialog.h"
#include "../mainwindow/mainwindow.h"
#include "../items/resizableboard.h"
#include "../items/moduleidnames.h"
#include "../utils/textutils.h"
#include "../utils/graphicsutils.h"
#include "../utils/folderutils.h"
#include "../referencemodel/referencemodel.h"
#include "../fapplication.h"

#include "binpacking/GuillotineBinPack.h"
#include "binpacking/Rect.h"

#include <QFile>
#include <QDomDocument>
#include <QDomElement>
#include <QDir>
#include <QTemporaryDir>
#include <qmath.h>
#include <limits>
#include <algorithm>
#include <cmath>

// ======================================================================
// Internal helpers — these wrap the legacy Panelizer API to ensure
// byte-identical output for the PR #1 golden-checksums test.
// ======================================================================

namespace PanelizerEngine {

// Per-copy placement record. Mirrors only the fields the panel emitter
// needs (board name/path, physical size, final position and rotation);
// it deliberately carries none of the legacy corner-stitching machinery.
struct LegacyPanelItem {
    QString boardName;
    QString path;
    int required = 0;
    int maxOptional = 0;
    int optionalPriority = 0;
    int produced = 0;
    QSizeF boardSizeInches;
    long boardID = 0;
    double x = 0.0, y = 0.0;
    bool rotate90 = false;

    LegacyPanelItem() = default;
};

// Convert SourceBoard to LegacyPanelItem
static LegacyPanelItem sourceToLegacy(const SourceBoard& source, int index)
{
    LegacyPanelItem item;
    item.path = source.fzzPath;
    item.boardName = QFileInfo(source.fzzPath).completeBaseName();
    item.boardSizeInches = source.boardSizeInches;
    item.required = source.copies;
    item.maxOptional = 0;
    item.optionalPriority = 0;
    item.boardID = index;
    return item;
}

// Convert LegacyPanelItem to PlacedBoard
static PlacedBoard legacyToPlaced(const LegacyPanelItem& item, const SourceBoard& source)
{
    PlacedBoard board;
    board.positionInches = QPointF(item.x, item.y);
    board.rotated90 = item.rotate90;
    board.boardId = item.boardName;
    board.source = source;
    return board;
}

} // namespace PanelizerEngine

// ======================================================================
// PanelizerEngine::layout() — GuillotineBinPack bin packing
// ======================================================================

namespace PanelizerEngine {

/**
 * @brief Lay the requested board copies out on the panel.
 *
 * Uses rbp::GuillotineBinPack — the same production rectangle packer
 * Fritzing already uses to pack a sketch onto a board (see
 * PCBSketchWidget) — with the best-area-fit choice heuristic and the
 * minimize-area split heuristic.
 *
 * Algorithm:
 * 1. Expand every SourceBoard into one entry per requested copy.
 * 2. Inflate each board by one gutter (right/bottom), scale inches to
 *    integer mils, sort largest-area first and Insert() each into a
 *    bin sized to the usable panel area (panel minus border).
 * 3. Translate the packed integer rectangles back to panel-local
 *    inches (offset by the border) and record rotation.
 *
 * Rotation: RectBestAreaFit only rotates a board when it would not
 * otherwise fit, so a reported rotation is a genuine necessity. If the
 * source forbids rotation (allowRotate90 == false) the layout fails
 * rather than silently rotating the board.
 *
 * Coordinates are in inches throughout, converted to mils only for the
 * packer and back again immediately afterwards.
 */
bool layout(const QList<SourceBoard>& sources,
            const PanelSpec&        spec,
            QList<PlacedBoard*>&    outItems,
            QString*                errorOut)
{
    outItems.clear();

    if (sources.isEmpty()) {
        if (errorOut) {
            *errorOut = "No source boards specified";
        }
        return false;
    }

    // == Step 1: Load board geometries ==
    // For each source board, we need to determine its physical size.
    // If it's an open window, we can query the PCB sketch widget.
    // If it's a .fzz file, we need to extract the board geometry.

    QList<LegacyPanelItem> legacyItems;
    QList<SourceBoard> remainingSources;

    for (int i = 0; i < sources.size(); ++i) {
        const SourceBoard& source = sources[i];
        LegacyPanelItem item = sourceToLegacy(source, i);

        // Determine board size — for now, use a default if we can't load
        // TODO: extract actual board size from .fzz or open window
        if (source.boardSizeInches.width() > 0 && source.boardSizeInches.height() > 0) {
            item.boardSizeInches = source.boardSizeInches;
        } else {
            // Default 100x100mm board (3.94x3.94 inches)
            item.boardSizeInches = QSizeF(3.94, 3.94);
        }

        // Add all copies to the legacyItems list
        for (int c = 0; c < source.copies; ++c) {
            legacyItems.append(item);
            remainingSources.append(source);
        }
    }

    if (legacyItems.isEmpty()) {
        if (errorOut) {
            *errorOut = "No valid boards to place";
        }
        return false;
    }

    // == Step 2: GuillotineBinPack in integer mils ==
    //
    // Replaces the bespoke shelf packer (and the abandoned legacy
    // corner-stitching Tile engine) with rbp::GuillotineBinPack, the
    // packer Fritzing already ships and uses elsewhere. The packer works
    // in integer units, so inches are scaled to thousandths of an inch.
    //
    // Coordinate system: the bin's (0,0) is the usable panel top-left
    // (panel minus border on every side); placements are shifted back by
    // borderInches when written to LegacyPanelItem.

    const double usableW = qMax(0.0, spec.panelSizeInches.width()  - 2 * spec.borderInches);
    const double usableH = qMax(0.0, spec.panelSizeInches.height() - 2 * spec.borderInches);
    if (usableW <= 0.0 || usableH <= 0.0) {
        if (errorOut) {
            *errorOut = QObject::tr("Panel size (%1x%2 in) is smaller than 2x border (%3 in)")
                            .arg(spec.panelSizeInches.width())
                            .arg(spec.panelSizeInches.height())
                            .arg(spec.borderInches);
        }
        return false;
    }

    // 1000 integer units per inch keeps sub-mil board dimensions distinct
    // while staying comfortably inside int range for any realistic panel.
    constexpr double kUnitsPerInch = 1000.0;
    auto toUnits = [](double inches) {
        return static_cast<int>(std::lround(inches * kUnitsPerInch));
    };

    rbp::GuillotineBinPack binPack(toUnits(usableW), toUnits(usableH));

    // Each board is inflated by one gutter on the right/bottom so that
    // packed neighbours never touch; the trailing gutter is part of the
    // usable-area budget. Pack largest-area boards first.
    const double gutter = spec.gutterInches;

    struct Cand { int idx; int w, h; };
    QList<Cand> work;
    work.reserve(legacyItems.size());
    for (int i = 0; i < legacyItems.size(); ++i) {
        Cand c;
        c.idx = i;
        c.w = toUnits(legacyItems[i].boardSizeInches.width()  + gutter);
        c.h = toUnits(legacyItems[i].boardSizeInches.height() + gutter);
        work.append(c);
    }
    std::sort(work.begin(), work.end(), [](const Cand & a, const Cand & b) {
        return (static_cast<long long>(a.w) * a.h) > (static_cast<long long>(b.w) * b.h);
    });

    for (const Cand & c : work) {
        LegacyPanelItem & item = legacyItems[c.idx];

        rbp::Rect packed = binPack.Insert(
            c.w, c.h, /*merge*/ true,
            rbp::GuillotineBinPack::RectBestAreaFit,
            rbp::GuillotineBinPack::SplitMinimizeArea);

        // A zero-area result means the board did not fit anywhere.
        if (packed.width == 0 || packed.height == 0) {
            if (errorOut) {
                *errorOut = QObject::tr("Failed to place board %1 (%2 x %3 in) on a %4 x %5 in panel")
                                .arg(item.boardName)
                                .arg(item.boardSizeInches.width())
                                .arg(item.boardSizeInches.height())
                                .arg(spec.panelSizeInches.width())
                                .arg(spec.panelSizeInches.height());
            }
            return false;
        }

        // The packer swaps width/height when it rotates the rectangle.
        const bool rotated = (packed.width != c.w);
        if (rotated && !remainingSources[c.idx].allowRotate90) {
            if (errorOut) {
                *errorOut = QObject::tr("Board %1 only fits when rotated 90\u00b0, "
                                        "but rotation is disabled for it")
                                .arg(item.boardName);
            }
            return false;
        }

        // packed.(x,y) is the top-left of the inflated rect; the board's
        // own top-left coincides with it because the gutter pads the far
        // edges. Shift back into panel-local inches past the border.
        item.x = spec.borderInches + packed.x / kUnitsPerInch;
        item.y = spec.borderInches + packed.y / kUnitsPerInch;
        item.rotate90 = rotated;
    }

    // == Step 3: Convert results to PlacedBoard ==
    for (int i = 0; i < legacyItems.size(); ++i) {
        PanelizerEngine::PlacedBoard* board = new PanelizerEngine::PlacedBoard();
        *board = legacyToPlaced(legacyItems[i], remainingSources[i]);
        outItems.append(board);
    }

    return true;
}

/**
 * @brief V-cut/mouse-bite SVG generation.
 *
 * V-cut:
 * - For each interior gutter line (vertical and horizontal),
 *   emit a single <line> in SVG into layer panel_vcut.
 * - Line extends border + epsilon past panel outline on each end.
 * - panel_vcut maps to Gerber .gko (board outline).
 *
 * Mouse-bites:
 * - Per gutter segment between two adjacent boards:
 *   1. Compute gutter midline.
 *   2. Lay out holesPerTab circles centered on midline.
 *   3. Emit two parallel arcs/lines on outline layer (slots).
 *   4. Holes go on .txt drill Gerber, slots on .gko.
 */
QString separationSvg(const QList<PanelizerEngine::PlacedBoard*>& items,
                      const PanelizerEngine::PanelSpec& spec,
                      const PanelizerEngine::SeparationSpec& sep)
{
	// Dispatch to the concrete separator implementation in
	// PanelizerSeparators. Each kind emits one <g id="edge_cuts">
	// group (and, for mouse-bites, an additional <g id="drill">).
	switch (sep.kind) {
	case PanelizerEngine::Separation::None:
		return QString();
	case PanelizerEngine::Separation::VCut:
		return PanelizerSeparators::vcutSvg(items, spec, sep);
	case PanelizerEngine::Separation::MouseBites:
		return PanelizerSeparators::mouseBiteSvg(items, spec, sep);
	}
	return QString();
}

/**
 * @brief Gerber emission implementation.
 *
 * Pattern: copy from MainWindow::exportToGerber() in
 * src/mainwindow/mainwindow_export.cpp#L1644.
 *
 * 1. For each PlacedBoard, call GerberGenerator::exportToGerber()
 *    with the placed board's ModelPart.
 * 2. Append synthetic elements:
 *    - V-cut lines → .gko layer
 *    - Mouse-bite holes → .txt drill layer
 *    - Mouse-bite slots → .gko layer
 *    - Fiducials → top copper + top soldermask
 *    - Tooling holes → .txt drill layer
 * 3. If savePanelFzz, create synthetic .fzz with PanelBoardItem.
 *
 * NOTE: This must run on the main thread (QGraphicsScene is not
 * thread-safe). Use ProcessEventBlocker for long phases.
 */
PanelizerEngine::Result emitPanel(const QList<PanelizerEngine::PlacedBoard*>& laidOut,
            const PanelizerEngine::PanelSpec&         spec,
            const PanelizerEngine::SeparationSpec&    sep,
            const PanelizerEngine::ExtrasSpec&        extras,
            const QString&           outputDir,
            FApplication*            app)
{
	// NOTE: Minimal first-stage emitter. Writes the separation +
	// extras SVG artifacts into outputDir so the user can verify
	// V-cut / mouse-bite geometry in any viewer.
	//
	// TODO(landracer): full GerberGenerator integration per board.
	// See MainWindow::exportToGerber() in mainwindow_export.cpp for
	// the pattern.
	(void)app;
	PanelizerEngine::Result result;

	if (laidOut.isEmpty()) {
		result.warnings << QObject::tr("emitPanel: no placed boards");
		return result;
	}

	QDir dir(outputDir);
	if (!dir.exists() && !dir.mkpath(".")) {
		result.warnings << QObject::tr("emitPanel: cannot create output dir %1").arg(outputDir);
		return result;
	}

	// Separation geometry → panel_separation.svg
	const QString sepSvg = separationSvg(laidOut, spec, sep);
	if (!sepSvg.isEmpty()) {
		QFile f(dir.filePath("panel_separation.svg"));
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			f.write(sepSvg.toUtf8());
			f.close();
		} else {
			result.warnings << QObject::tr("emitPanel: cannot write panel_separation.svg");
		}
	}

	// Extras (fiducials + tooling holes) → panel_extras.svg
	QString extrasSvg;
	if (extras.addFiducials) {
		extrasSvg += PanelizerSeparators::fiducialSvg(spec,
			extras.fiducialDiameterMils, extras.fiducialClearMils);
	}
	if (extras.addToolingHoles) {
		extrasSvg += PanelizerSeparators::toolingHoleSvg(spec,
			extras.toolingHoleDiameterInches);
	}
	if (!extrasSvg.isEmpty()) {
		QFile f(dir.filePath("panel_extras.svg"));
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			f.write(extrasSvg.toUtf8());
			f.close();
		}
	}

	// Composite the full production bundle (Edge_Cuts.gbr + .drl +
	// placeholder copper/silk/mask + BOM/CPL + .gbrjob) via FabExporter.
	// NOTE: sketchBoard / sketchWidget are null in this code path because
	// emitPanel does not yet load the synthetic panel sketch. PR #G6
	// will surface them so per-board copper rendering can land.
	{
		FabExporter exporter;
		FabExporter::Spec fspec;
		fspec.profile     = FabExporter::JLCPCB;
		fspec.projectName = QFileInfo(outputDir).fileName();
		if (fspec.projectName.isEmpty()) fspec.projectName = QStringLiteral("panel");
		fspec.outputDir   = outputDir;
		FabExporter::Result fr = exporter.exportFromPanel(
			laidOut, spec, sep, extras,
			/*sketchBoard=*/nullptr, /*sketchWidget=*/nullptr, fspec);
		result.gerberFiles  = fr.written;
		result.warnings.append(fr.warnings);
		if (!fr.success) {
			result.warnings << QObject::tr("emitPanel: FabExporter bundle failed");
		}
		// Prefer the production gerber directory over the artifact dir.
		if (!fr.productionDir.isEmpty()) {
			result.gerberDir = QDir(fr.productionDir).filePath("gerber");
		} else {
			result.gerberDir = dir.absolutePath();
		}
	}
	result.success = true;
	return result;
}

} // namespace PanelizerEngine
