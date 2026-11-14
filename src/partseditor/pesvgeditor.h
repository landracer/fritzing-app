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

#ifndef PESVGEDITOR_H
#define PESVGEDITOR_H

#include <QWidget>
#include <QDomDocument>
#include <QString>
#include <QStringList>
#include <QPointF>
#include <QVector>

class QGraphicsScene;
class QGraphicsView;
class QGraphicsRectItem;
class QLabel;
class QVBoxLayout;

// The clean-parts-editor surface (docs/design/pe-selfpaint-renderer.md, endpoint of pe-sole-renderer.md).
//
// Sole self-paint renderer + editing surface. The DOM is decomposed by PEItemPlan::plan: a group containing
// editable id'd primitives is descended so each (pad, hole, track, ...) becomes its own PESelfPaintItem
// (id under data(0)) that can be hit-tested and moved, while a group of opaque id-less art (breadboard body,
// deferred path/text) becomes one non-selectable PESelfPaintItem that paints its whole subtree -- never both
// for the same subtree, so no element is painted twice. Before binding, each planned element's inherited
// transform is flattened into absolute geometry (PETransform::flatten, idempotent), so each item paints its
// decoded QPainterPath at the identity transform and its boundingRect agrees with the painted geometry.
// Editing mutates the element's geometry attributes in the owned QDomDocument (by id) and rebuilds -- so the
// picture is always exactly what the document says. Selection shows a dashed outline (from
// PEPrimGeom::localBounds). A body-drag PREVIEWS by offsetting the item's position and on release commits the
// move to the DOM. A grip-drag PREVIEWS by live-writing the resized box into the DOM (resizeTo) so the shape
// grows from the grabbed edge rather than sliding, and commits on release. A <path> is edited via its decoded
// anchors. Undo/redo replays move deltas and resize from/to boxes exactly.
class PESvgEditor : public QWidget
{
	Q_OBJECT

public:
	explicit PESvgEditor(QWidget * parent = nullptr);
	~PESvgEditor() override;

	void setDocument(const QDomDocument & doc, const QString & svgLabel);
	void setVisibleLayers(const QStringList & layerGroupIds);
	void fitToWindow();

	// ---- R2: measurement / units -------------------------------------------------
	// The unit the readout (selection bounds + cursor position) is expressed in. Every editor starts
	// in PEUnits::readPreference() (the stored preference, or its mm default); setUnits() switches it
	// live, persists it via PEUnits::writePreference() so the whole app agrees on the next launch, and
	// re-formats the current selection's readout. The unit is DISPLAY ONLY -- geometry in the DOM stays
	// in svg user units (px at 90 dpi); changing it never rewrites an attribute.
	QString units() const { return m_units; }
	void setUnits(const QString & unit);
	// The unit a newly created editor starts in (the stored preference / mm default). The shells read
	// this once so they can seed every editor identically.
	static QString defaultUnits();

	// Interaction entry points -- called by PESvgEditorView with scene-space points. Public so the
	// selection/move/resize/anchor behavior is directly testable as a controller (the view is a thin forwarder).
	void handlePress(const QPointF & scenePos);
	void handleMove(const QPointF & scenePos);
	void handleRelease(const QPointF & scenePos);
	// R2: a hover (no button held) point. Emits cursorMoved() with the scene point so the shell can show
	// it in the current unit; never mutates the DOM or the selection.
	void handleHover(const QPointF & scenePos);

	// True when there is an undoable move committed.
	bool canUndo() const;
	bool canRedo() const;

	const QDomDocument & document() const { return m_doc; }
	QString currentFilepath() const { return m_filepath; }
	void setFilepath(const QString & path) { m_filepath = path; }

	// Serialise the CURRENT DOM and write it to `path` (UTF-8). When `path` is empty the editor's current
	// filepath is used (so Save targets the file the part was opened from). On success m_filepath is set to
	// the written path, the dirty flag clears, and dirtyChanged(false)/undoRedoChanged() re-emit. On failure
	// (empty/unset path, or the file can't be written) it returns false and leaves dirty state untouched.
	// Raw-source-of-truth: the editor ALWAYS holds the svg in its raw on-disk bytes (library svgs already
	// author each layer's colour per layer-group, so no load-time recolouring is applied; view selection is
	// via setVisibleLayers). So a save round-trips the file faithfully: geometry edits persist in full, and
	// no view colours are ever written into the file.
	bool saveToPath(const QString & path = QString());

public Q_SLOTS:
	void undo();
	void redo();

Q_SIGNALS:
	void elementSelected(const QString & elementId);
	void selectionInfo(const QString & text);   // "id · kind · <bounds in the current unit>" for the status bar
	void dirtyChanged(bool dirty);
	void undoRedoChanged();
	void saved(const QString & path);            // emitted after a successful saveToPath
	// R2: the pointer's scene position in svg user units, alongside the same position pre-formatted for
	// the current unit. Kept as raw px too so a shell that wants the numbers (a future field editor)
	// does not have to re-parse the string.
	void cursorMoved(const QPointF & pxPoint, const QString & formatted);
	// R2: the readout unit changed (the shells re-seed any dependent chrome).
	void unitsChanged(const QString & unit);

protected:
	void rebuildScene();
	void resizeEvent(QResizeEvent * event) override;

private:
	// The view (PESvgEditorView) calls the public handle* with scene-space points.

	QDomElement selectedElement() const;         // element for m_selectedId (null if none)
	void updateSelectionVisual();                // (re)draw the dashed outline over the selection

	// Slice D selection chrome: the 8 marquee grips (4 corners + 4 edge midpoints) around the selected
	// element's bounds, and (for a <path>) its decoded segment anchors as node grips. All in scene coords
	// (elements are flattened to absolute, so these are directly scene points). handlePress routes a grabbed
	// grip to resizeTo, a grabbed node to editPathAnchor, and a body press to the smallest-area body select.
	enum Grip {
		GripNone        = -1,
		GripLeft        = 1 << 0,
		GripRight       = 1 << 1,
		GripTop         = 1 << 2,
		GripBottom      = 1 << 3
	};
	// The scene-space handles: a grip is (mask, small square rect); a node grip is (index>=0, point).
	// Resize grips are drawn only for kinds PEPrimGeom::isResizable accepts (rect/ellipse/line/polyline/
	// polygon). A <circle> is round (single r) so it has no honest per-axis box resize and shows no grips --
	// it is move-only; a <path> is edited node by node via anchorGrips().
	struct GripHandle { int mask; QRectF rect; };
	QVector<GripHandle> resizeGrips() const;      // 8 handles over localBounds(selectedElement())
	QVector<QPointF> anchorGrips() const;         // path anchors of the selection (empty if not a path)
	// Which resize-grip mask covers the scene point (GripNone if none).
	int hitResizeGrip(const QPointF & scenePoint) const;
	// Which anchor node index is at the scene point within tolerance (user units), -1 if none.
	int hitAnchorGrip(const QPointF & scenePoint, qreal tolerance) const;

	// The marquee rect (selected element's absolute bounds) in scene coords, or empty if none.
	QRectF selectionRect() const;

	// One undoable edit. The tag picks which fields apply: Move stores a translation delta (undo = -delta);
	// Resize stores the box before (undo) and after (redo); Anchor stores which segment moved and its anchor
	// before (undo) and after (redo). Replay/undo just apply the stored before/after.
	struct UndoEntry {
		enum class Kind { Move, Resize, Anchor };
		Kind kind = Kind::Move;
		QString id;
		QPointF delta;              // Move (applied forward on redo, negated on undo)
		QRectF  from, to;           // Resize (restore `from` on undo, apply `to` on redo)
		int     segmentIndex = -1;  // Anchor
		QPointF anchorFrom;         // Anchor (restore on undo)
		QPointF anchorTo;           // Anchor (apply on redo)
	};

	// Push an edit onto the undo stack (dropping any redo tail) and signal dirty/undo-redo changed.
	void pushUndo(const UndoEntry & e);

	bool m_needInitialFit = false;

	QGraphicsScene * m_scene = nullptr;
	QGraphicsView * m_view = nullptr;
	QGraphicsRectItem * m_selection = nullptr;   // dashed marquee
	QVector<QGraphicsRectItem *> m_handles;        // the visible grip/node squares (zoom-independent)
	QLabel * m_svgLabel = nullptr;
	QVBoxLayout * m_layout = nullptr;
	QDomDocument m_doc;
	QString m_label;
	QString m_filepath;
	QStringList m_visibleLayers;
	// R2: the readout unit for this editor (never affects stored geometry).
	QString m_units = defaultUnits();

	// Selection / drag state.
	QString m_selectedId;
	bool m_dragging = false;
	QPointF m_dragSceneStart;      // scene pos at press
	QPointF m_dragDeltaScene;      // scene-space delta accumulated during drag

	// Which chrome affordance (if any) is being dragged, and the origin needed to commit it.
	int   m_dragGrip = GripNone;   // resize mask held, or GripNone
	bool  m_draggingResize = false;
	int   m_dragAnchor = -1;       // anchor segment index held, or -1
	QRectF m_resizeOrigin;         // element bounds (absolute) at resize-drag start
	QVector<UndoEntry> m_undo;     // tagged move/resize/anchor history
	int m_undoPos = 0;             // number of committed edits that can be undone
	bool m_dirty = false;
};

#endif // PESVGEDITOR_H

