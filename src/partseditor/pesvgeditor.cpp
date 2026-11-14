// PESvgEditor -- sole-renderer parts editing surface (docs/design/pe-sole-renderer.md).
// SPDX-License-Identifier: GPL-3.0-or-later   Copyright (c) 2026 Fritzing

#include "pesvgeditor.h"
#include "peprimitivegeom.h"
#include "peitemplan.h"
#include "petransform.h"
#include "peselfpaintitem.h"
#include "peunits.h"
#include "../utils/textutils.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QDomElement>
#include <QPen>
#include <QLineF>
#include <QtDebug>
#include <functional>

// On-screen size (pixels) of a selection grip / node mark square. Zoom-independent: divided by the view
// scale when placed in scene coords, so it stays this many device px regardless of zoom and is always grabbable.
static constexpr qreal kGripPx = 7.0;

// One PESelfPaintItem per planned unit (a leaf with an id, or an opaque art group), each self-painting its
// own decoded QPainterPath. Per-leaf items -- rather than one item per layer group -- are what let the
// editor hit-test, select and move an INDIVIDUAL graphic (a pad) instead of its whole layer. Because each
// item paints ONLY the primitive(s) PEItemPlan assigned to it (no QtSvg whole-doc-clip repaint), the nested
// copper0>copper1 case is handled without ever painting the same pixels twice -- the double-draw that QtSvg
// isolation caused is structurally impossible here.

// The view forwards mouse events to the editor in scene coordinates. It does NOT use Qt's built-in
// item drag; the editor translates the element in the DOM itself so the document stays the source of
// truth and the render is always what the file says.
class PESvgEditorView : public QGraphicsView
{
public:
	explicit PESvgEditorView(QGraphicsScene * scene, PESvgEditor * editor)
		: QGraphicsView(scene), m_editor(editor) {}

protected:
	void mousePressEvent(QMouseEvent * ev) override {
		if (ev->button() == Qt::LeftButton) {
			m_editor->handlePress(mapToScene(ev->pos()));
			ev->accept();
			return;
		}
		QGraphicsView::mousePressEvent(ev);
	}
	void mouseMoveEvent(QMouseEvent * ev) override {
		// R2: the cursor readout follows the pointer whether or not a drag is in progress, so the
		// position is always live. Only the drag itself is consumed; plain hover falls through.
		if (ev->buttons() & Qt::LeftButton) {
			m_editor->handleMove(mapToScene(ev->pos()));
			m_editor->handleHover(mapToScene(ev->pos()));
			ev->accept();
			return;
		}
		m_editor->handleHover(mapToScene(ev->pos()));
		QGraphicsView::mouseMoveEvent(ev);
	}
	void mouseReleaseEvent(QMouseEvent * ev) override {
		if (ev->button() == Qt::LeftButton) {
			m_editor->handleRelease(mapToScene(ev->pos()));
			ev->accept();
			return;
		}
		QGraphicsView::mouseReleaseEvent(ev);
	}

private:
	PESvgEditor * m_editor;
};

PESvgEditor::PESvgEditor(QWidget * parent)
	: QWidget(parent)
{
	m_scene = new QGraphicsScene(this);

	m_view = new PESvgEditorView(m_scene, this);
	m_view->setRenderHint(QPainter::Antialiasing, true);
	m_view->setRenderHint(QPainter::SmoothPixmapTransform, true);
	m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	m_view->setBackgroundBrush(QColor(0x40, 0x40, 0x40));
	// R2: hover (no button held) must reach mouseMoveEvent so the cursor readout stays live.
	m_view->setMouseTracking(true);

	m_svgLabel = new QLabel(this);
	m_svgLabel->setTextFormat(Qt::PlainText);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->addWidget(m_view, 1);
	m_layout->addWidget(m_svgLabel);
}

PESvgEditor::~PESvgEditor()
{
	// No renderer member any more (self-paint items own their decoded geometry).
}

void PESvgEditor::setDocument(const QDomDocument & doc, const QString & svgLabel)
{
	m_doc = doc;
	m_label = svgLabel;
	m_svgLabel->setText(svgLabel);
	m_selectedId.clear();
	m_undo.clear();
	m_undoPos = 0;
	m_dirty = false;
	emit dirtyChanged(false);
	emit undoRedoChanged();
	rebuildScene();
}

void PESvgEditor::setVisibleLayers(const QStringList & layerGroupIds)
{
	m_visibleLayers = layerGroupIds;
	rebuildScene();
}

bool PESvgEditor::canUndo() const { return m_undoPos > 0; }
bool PESvgEditor::canRedo() const { return m_undoPos < m_undo.size(); }

// R2: the unit a fresh editor starts in is the stored preference (or its mm default). Read once per
// construction; PEUnits::readPreference already maps an unknown/corrupt stored value back to the default.
QString PESvgEditor::defaultUnits()
{
	return PEUnits::readPreference();
}

// R2: switch the readout unit. Persist it so every editor (and the next launch) agrees, re-format the
// CURRENT selection so the marquee's readout updates without a re-select, and announce the change.
// Display-only: no geometry attribute is touched, so this never dirties the document.
void PESvgEditor::setUnits(const QString & unit)
{
	if (!PEUnits::isKnownUnit(unit)) return;   // honest no-op, never a silent wrong scale
	if (unit == m_units) return;
	m_units = unit;
	PEUnits::writePreference(unit);
	if (!m_selectedId.isEmpty() && !selectedElement().isNull()) {
		const QDomElement el = selectedElement();
		emit selectionInfo(QStringLiteral("%1 · %2 · %3")
			.arg(m_selectedId, PEPrimGeom::kindName(PEPrimGeom::kindFor(el)),
			     PEUnits::formatBox(PEPrimGeom::localBounds(el), m_units)));
	}
	emit unitsChanged(m_units);
}

// R2: hover is read-only -- it feeds the cursor coordinate readout and nothing else. Explicitly NOT
// a DOM edit: it never touches m_dragging/m_selectedId, so hovering can neither select nor dirty.
void PESvgEditor::handleHover(const QPointF & scenePos)
{
	emit cursorMoved(scenePos, PEUnits::formatPoint(scenePos, m_units));
}

QDomElement PESvgEditor::selectedElement() const
{
	if (m_selectedId.isEmpty()) return QDomElement();
	return PEPrimGeom::findById(m_doc.documentElement(), m_selectedId);
}

// Sole-renderer (self-paint) itemization -- the one render, no duplication. PEItemPlan::plan() decides the
// disjoint set of elements to draw (pure DOM, golden-tested); here each becomes exactly ONE PESelfPaintItem.
// Before binding, each planned element is flattened (PETransform::flatten, idempotent) so its inherited
// transform is baked into absolute geometry attrs and no 'transform' remains -- PESelfPaintItem then paints
// its primitive(s) at the identity transform, so the decoded path IS the scene geometry. Editable leaves
// carry their id under data(0) (hit-test/move resolve it) and are selectable; opaque group items carry no
// data(0) -> they render faithfully but are not selectable (editing deferred). `visibleLayers` filters
// top-level layer groups.
void PESvgEditor::rebuildScene()
{
	m_scene->clear();                 // deletes items AND the chrome (marquee + handles) -- they are scene items
	m_selection = nullptr;            // clear() deleted them; drop the dangling pointers so updateSelectionVisual
	m_handles.clear();                // (called at the end here) never iterates freed items.

	if (m_doc.isNull()) {
		return;
	}

	const QDomElement root = m_doc.documentElement();
	const QVector<PEItemPlan::Planned> planned = PEItemPlan::plan(root, m_visibleLayers);
	for (const PEItemPlan::Planned & p : planned) {
		QDomElement el = PEPrimGeom::findById(root, p.id);
		if (el.isNull()) continue;
		// Make geometry absolute + transform-free so the self-paint item renders at identity. Non-destructive
		// to the *logical* geometry: this is the established load-time bake (idempotent across re-renders).
		PETransform::flatten(el);
		auto * item = new PESelfPaintItem();
		item->setElement(el);
		if (p.editable) {
			item->setData(0, p.id);   // id carried on the item for hit-testing/move
			item->setEditable(true);
		}
		m_scene->addItem(item);
	}

	m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-8, -8, 8, 8));
	m_needInitialFit = true;
	if (m_view->viewport()->width() > 1 && m_view->viewport()->height() > 1) {
		fitToWindow();
	}
	// Re-assert selection outline if the selected id still resolves (survives a redo-driven reload).
	updateSelectionVisual();
}

QRectF PESvgEditor::selectionRect() const
{
	if (m_selectedId.isEmpty()) return QRectF();
	return PEPrimGeom::localBounds(selectedElement());
}

// The 8 resize handles over the selection's absolute bounds, each a small square centred on a corner or
// edge midpoint. Size is zoom-independent (a constant on-screen pixel size divided by the view scale), so a
// grip is as grabbable zoomed out as zoomed in. Corner masks combine two edges (e.g. GripTop|GripLeft).
QVector<PESvgEditor::GripHandle> PESvgEditor::resizeGrips() const
{
	QVector<GripHandle> out;
	const QRectF b = selectionRect();
	if (!b.isValid()) return out;
	const qreal s = (m_view && m_view->transform().m11() != 0.0) ? m_view->transform().m11() : 1.0;
	const qreal half = (kGripPx / s) / 2.0;
	const qreal cx = b.center().x(), cy = b.center().y();
	auto mk = [&](int mask, QPointF c) { return GripHandle{ mask, QRectF(c.x() - half, c.y() - half, half * 2, half * 2) }; };
	out << mk(GripTop    | GripLeft,  b.topLeft())
	    << mk(GripTop    | GripRight, b.topRight())
	    << mk(GripBottom | GripLeft,  b.bottomLeft())
	    << mk(GripBottom | GripRight, b.bottomRight())
	    << mk(GripTop,    QPointF(cx, b.top()))
	    << mk(GripBottom, QPointF(cx, b.bottom()))
	    << mk(GripLeft,   QPointF(b.left(),  cy))
	    << mk(GripRight,  QPointF(b.right(), cy));
	return out;
}

// The decoded path anchors of the selection, in absolute scene coords (empty if not a path).
QVector<QPointF> PESvgEditor::anchorGrips() const
{
	if (m_selectedId.isEmpty()) return QVector<QPointF>();
	return PEPrimGeom::pathAnchors(selectedElement());
}

// Which resize-grip square the scene point is on. Nearest-center within half a grip wins.
int PESvgEditor::hitResizeGrip(const QPointF & scenePoint) const
{
	const qreal s = (m_view && m_view->transform().m11() != 0.0) ? m_view->transform().m11() : 1.0;
	const qreal tol = (kGripPx / s);
	int best = GripNone;
	qreal bestDist = 1e30;
	for (const GripHandle & g : resizeGrips()) {
		const QPointF c = g.rect.center();
		const qreal d = QLineF(c, scenePoint).length();
		if (d <= tol / 2.0 && d < bestDist) { bestDist = d; best = g.mask; }
	}
	return best;
}

// Which path-anchor node (segment index) is within `tolerance` (user units) of the scene point; -1 if none.
int PESvgEditor::hitAnchorGrip(const QPointF & scenePoint, qreal tolerance) const
{
	const QVector<QPointF> pts = anchorGrips();
	int best = -1;
	qreal bestDist = 1e30;
	for (int i = 0; i < pts.count(); ++i) {
		const qreal d = QLineF(pts.at(i), scenePoint).length();
		if (d <= tolerance && d < bestDist) { bestDist = d; best = i; }
	}
	return best;
}

void PESvgEditor::updateSelectionVisual()
{
	// Tear down the previous chrome (marquee + all grip/node squares).
	auto removeItem = [this](QGraphicsItem * it) {
		if (!it) return;
		if (it->scene()) it->scene()->removeItem(it);
		delete it;
	};
	removeItem(m_selection);
	m_selection = nullptr;
	for (QGraphicsRectItem * h : m_handles) removeItem(h);
	m_handles.clear();

	if (m_selectedId.isEmpty()) return;
	const QDomElement el = selectedElement();
	if (el.isNull()) return;

	// Marquee: a dashed, cosmetic (zoom-independent) outline of the element's absolute bounds. It lives at
	// the scene (not parented) so it never inherits a transform -- geometry is already absolute.
	const QRectF b = PEPrimGeom::localBounds(el);
	if (!b.isValid()) return;
	QPen pen(QColor(0x2a, 0xa0, 0xff));
	pen.setWidth(0);
	pen.setStyle(Qt::DashLine);
	pen.setCosmetic(true);
	m_selection = new QGraphicsRectItem(b);
	m_selection->setPen(pen);
	m_selection->setBrush(Qt::NoBrush);
	m_selection->setZValue(1000);
	m_scene->addItem(m_selection);

	const QColor fill = (PEPrimGeom::kindFor(el) == PEPrimGeom::Kind::Path) ? Qt::white
	                                                                       : QColor(0x2a, 0xa0, 0xff);

	// Resize grips: drawn for every kind PEPrimGeom::isResizable accepts (rect/ellipse/line/polyline/polygon; a
	// circle is round so it is not resizable and gets none), not just rect -- a grip-drag resizes any of them (R1).
	// A <path> is not box-resizable; it is edited node by node below.
	if (PEPrimGeom::isResizable(el)) {
		for (const GripHandle & g : resizeGrips()) {
			auto * gi = new QGraphicsRectItem(g.rect);
			gi->setPen(pen);
			gi->setBrush(fill);
			gi->setZValue(1001);
			m_scene->addItem(gi);
			m_handles.append(gi);
		}
	}

	// Node grips: for a <path>, a small cosmetic square at each decoded segment anchor (draggable node).
	if (PEPrimGeom::kindFor(el) == PEPrimGeom::Kind::Path) {
		const qreal s = (m_view && m_view->transform().m11() != 0.0) ? m_view->transform().m11() : 1.0;
		const qreal half = (kGripPx / s) / 2.0;
		for (const QPointF & a : anchorGrips()) {
			auto * ai = new QGraphicsRectItem(QRectF(a.x() - half, a.y() - half, half * 2, half * 2));
			ai->setPen(pen);
			ai->setBrush(fill);
			ai->setZValue(1001);
			m_scene->addItem(ai);
			m_handles.append(ai);
		}
	}
}

void PESvgEditor::fitToWindow()
{
	if (m_scene->sceneRect().isEmpty()) return;
	m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
	m_needInitialFit = false;
}

void PESvgEditor::resizeEvent(QResizeEvent * event)
{
	QWidget::resizeEvent(event);
	if (m_needInitialFit && m_view->viewport()->width() > 1 && m_view->viewport()->height() > 1) {
		fitToWindow();
	}
}

// ---- interaction ---------------------------------------------------------------

void PESvgEditor::pushUndo(const UndoEntry & e)
{
	m_undo.resize(m_undoPos);     // drop any redo tail
	m_undo.append(e);
	m_undoPos = m_undo.size();
	m_dirty = true;
	emit dirtyChanged(true);
	emit undoRedoChanged();
}

void PESvgEditor::handlePress(const QPointF & scenePos)
{
	// If a grip/node of the CURRENT selection is grabbed, that takes priority over re-hitting the body.
	if (!m_selectedId.isEmpty()) {
		const QDomElement sel = selectedElement();
		const PEPrimGeom::Kind k = PEPrimGeom::kindFor(sel);
		// A <path> is node-edited, never box-resized; every other resizable kind gets the 8 grips.
		if (k == PEPrimGeom::Kind::Path) {
			const qreal s = (m_view && m_view->transform().m11() != 0.0) ? m_view->transform().m11() : 1.0;
			const int a = hitAnchorGrip(scenePos, kGripPx / s);
			if (a >= 0) {
				m_dragging = true; m_dragAnchor = a;
				m_dragSceneStart = scenePos; m_dragDeltaScene = QPointF(0, 0);
				return;
			}
		} else if (PEPrimGeom::isResizable(sel)) {
			const int grip = hitResizeGrip(scenePos);
			if (grip != GripNone) {
				m_dragging = true; m_draggingResize = true; m_dragGrip = grip;
				m_dragSceneStart = scenePos; m_dragDeltaScene = QPointF(0, 0);
				m_resizeOrigin = PEPrimGeom::localBounds(sel);
				return;
			}
		}
	}

	const QString id = PESelfPaintItem::hitTestId(m_scene, scenePos);
	if (id.isEmpty()) {
		m_selectedId.clear();
		updateSelectionVisual();
		emit elementSelected(QString());
		emit selectionInfo(QString());
		return;
	}

	m_selectedId = id;
	m_dragging = true;
	m_dragSceneStart = scenePos;
	m_dragDeltaScene = QPointF(0, 0);

	const QDomElement el = selectedElement();
	// R2: the bounds are converted+formatted by the pure, golden-tested PEUnits, in the editor's
	// current unit -- the readout is a projection of the stored px geometry, never a re-scaled model.
	const QRectF b = PEPrimGeom::localBounds(el);
	emit elementSelected(id);
	emit selectionInfo(QStringLiteral("%1 · %2 · %3")
		.arg(id, PEPrimGeom::kindName(PEPrimGeom::kindFor(el)),
		     PEUnits::formatBox(b, m_units)));
	updateSelectionVisual();
}

void PESvgEditor::handleMove(const QPointF & scenePos)
{
	if (!m_dragging || m_selectedId.isEmpty()) return;

	// A resize-drag RESIZES live: write the new box into the DOM (not a translate) and re-bind, so the shape
	// grows from the grabbed edge while dragging. m_resizeOrigin is the pre-drag box the point-list kinds scale
	// from, so a mid-drag recompute against the current (already-resized) geometry stays stable.
	if (m_draggingResize) {
		const QPointF delta = scenePos - m_dragSceneStart;
		qreal L = m_resizeOrigin.left(), R = m_resizeOrigin.right();
		qreal T = m_resizeOrigin.top(),  B = m_resizeOrigin.bottom();
		if (m_dragGrip & GripLeft)   L += delta.x();
		if (m_dragGrip & GripRight)  R += delta.x();
		if (m_dragGrip & GripTop)    T += delta.y();
		if (m_dragGrip & GripBottom) B += delta.y();
		const QRectF live = QRectF(QPointF(L, T), QPointF(R, B)).normalized();
		QDomElement el = selectedElement();
		if (el.isNull()) return;
		// Live DOM write: refresh the item's geometry, then refresh the chrome so the marquee + grips track.
		if (PEPrimGeom::resizeTo(el, m_resizeOrigin, live)) {
			for (QGraphicsItem * it : m_scene->items()) {
				if (it->data(0).toString() == m_selectedId) {
					if (auto * spi = static_cast<PESelfPaintItem *>(it)) spi->setElement(el);
					break;
				}
			}
			updateSelectionVisual();
		}
		return;
	}

	// Body/move preview is a position offset on the item (absolute==scene); the DOM commit happens on release.
	m_dragDeltaScene = scenePos - m_dragSceneStart;
	for (QGraphicsItem * it : m_scene->items()) {
		if (it->data(0).toString() == m_selectedId) { it->setPos(m_dragDeltaScene); break; }
	}
}

void PESvgEditor::handleRelease(const QPointF & scenePos)
{
	if (!m_dragging) return;
	m_dragging = false;

	const QString id = m_selectedId;
	if (id.isEmpty()) return;
	QDomElement el = selectedElement();
	if (el.isNull()) return;

	// The release point finalises the drag: fold it into the delta so a press-release with no intervening
	// move (or a move to an earlier point) still commits up to the real release position.
	m_dragDeltaScene = scenePos - m_dragSceneStart;

	// Anchor node edit: commit the dragged anchor to the segment's new absolute point.
	if (m_dragAnchor >= 0) {
		const int seg = m_dragAnchor; m_dragAnchor = -1;
		const QVector<QPointF> before = PEPrimGeom::pathAnchors(el);
		const QPointF from = (seg < before.count()) ? before.at(seg) : QPointF();
		const QPointF to = scenePos;
		if (to != from && PEPrimGeom::editPathAnchor(el, seg, to)) {
			UndoEntry e; e.kind = UndoEntry::Kind::Anchor; e.id = id;
			e.segmentIndex = seg; e.anchorFrom = from; e.anchorTo = to;
			pushUndo(e);
			rebuildScene();
		}
		return;
	}

	// Resize commit. handleMove already wrote the live box into the DOM during the drag; re-apply the fit to
	// the FINAL release point so a press-release with no intervening move (or a move to an earlier point) still
	// commits up to the real release position. The element's current box == the live box, so this is idempotent.
	if (m_draggingResize) {
		m_draggingResize = false;
		const int grip = m_dragGrip; m_dragGrip = GripNone;
		const QPointF delta = m_dragDeltaScene;
		const QRectF cur = PEPrimGeom::localBounds(el);
		qreal L = m_resizeOrigin.left(), R = m_resizeOrigin.right();
		qreal T = m_resizeOrigin.top(),  B = m_resizeOrigin.bottom();
		if (grip & GripLeft)   L += delta.x();
		if (grip & GripRight)  R += delta.x();
		if (grip & GripTop)    T += delta.y();
		if (grip & GripBottom) B += delta.y();
		const QRectF to = QRectF(QPointF(L, T), QPointF(R, B)).normalized();
		if (to != cur && PEPrimGeom::resizeTo(el, cur, to)) {
			UndoEntry e; e.kind = UndoEntry::Kind::Resize; e.id = id; e.from = m_resizeOrigin; e.to = to;
			pushUndo(e);
			rebuildScene();
		}
		return;
	}

	// Body move commit.
	const QPointF userDelta = m_dragDeltaScene;
	if (qAbs(userDelta.x()) < 1e-6 && qAbs(userDelta.y()) < 1e-6) return;
	if (!PEPrimGeom::moveBy(el, userDelta)) {
		for (QGraphicsItem * it : m_scene->items())
			if (it->data(0).toString() == id) { it->setPos(QPointF(0, 0)); break; }
		updateSelectionVisual();
		return;
	}
	UndoEntry e; e.kind = UndoEntry::Kind::Move; e.id = id; e.delta = userDelta;
	pushUndo(e);
	rebuildScene();
}


void PESvgEditor::undo()
{
	if (!canUndo()) return;
	const UndoEntry e = m_undo.at(m_undoPos - 1);
	m_undoPos--;
	QDomElement el = PEPrimGeom::findById(m_doc.documentElement(), e.id);
	if (!el.isNull()) {
		switch (e.kind) {
			case UndoEntry::Kind::Move:   PEPrimGeom::moveBy(el, -e.delta); break;
			// Restore the pre-resize box: fit the current (after) geometry back to e.from.
			case UndoEntry::Kind::Resize: PEPrimGeom::resizeTo(el, PEPrimGeom::localBounds(el), e.from); break;
			case UndoEntry::Kind::Anchor: PEPrimGeom::editPathAnchor(el, e.segmentIndex, e.anchorFrom); break;
		}
	}
	m_selectedId = e.id;
	m_dirty = true;
	emit undoRedoChanged();
	rebuildScene();
}

void PESvgEditor::redo()
{
	if (!canRedo()) return;
	const UndoEntry e = m_undo.at(m_undoPos);
	m_undoPos++;
	QDomElement el = PEPrimGeom::findById(m_doc.documentElement(), e.id);
	if (!el.isNull()) {
		switch (e.kind) {
			case UndoEntry::Kind::Move:   PEPrimGeom::moveBy(el, e.delta); break;
			// Re-apply the resize box: fit the current (restored) geometry to e.to.
			case UndoEntry::Kind::Resize: PEPrimGeom::resizeTo(el, PEPrimGeom::localBounds(el), e.to); break;
			case UndoEntry::Kind::Anchor: PEPrimGeom::editPathAnchor(el, e.segmentIndex, e.anchorTo); break;
		}
	}
	m_selectedId = e.id;
	m_dirty = true;
	emit undoRedoChanged();
	rebuildScene();
}

// Serialise the current DOM to disk. `path` empty -> write back to the file the part/svg was opened from.
// The DOM is serialised whole (toString(0), byte-faithful, no re-indent) and written UTF-8 via the app's
// TextUtils::writeUtf8 (same helper the app's own model/save path uses). Because the editor always holds the
// svg's RAW on-disk bytes -- colourisation is applied at render time (PEItemPlan/PETransform), never baked
// into m_doc -- this round-trips the file faithfully: geometry edits persist, view colours never leak in.
bool PESvgEditor::saveToPath(const QString & path)
{
	const QString target = path.isEmpty() ? m_filepath : path;
	if (target.isEmpty()) return false;          // nothing to save to (no current filepath) and none given
	if (m_doc.isNull()) return false;            // nothing loaded

	if (!TextUtils::writeUtf8(target, m_doc.toString(0))) return false;

	m_filepath = target;
	m_dirty = false;
	emit dirtyChanged(false);
	emit undoRedoChanged();
	emit saved(target);
	return true;
}


