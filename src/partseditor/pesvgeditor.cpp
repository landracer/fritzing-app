// PESvgEditor -- sole-renderer parts editing surface (docs/design/pe-sole-renderer.md).
// SPDX-License-Identifier: GPL-3.0-or-later   Copyright (c) 2026 Fritzing

#include "pesvgeditor.h"
#include "peprimitivegeom.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsSvgItem>
#include <QGraphicsRectItem>
#include <QSvgRenderer>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QDomElement>
#include <QPen>
#include <QtDebug>
#include <functional>

// One QGraphicsSvgItem per modelled graphic (a leaf with an id + computable localBounds), rendered
// through a single shared QSvgRenderer by element id. Per-leaf items -- rather than one item per
// layer group -- are what let the editor hit-test, select and move an INDIVIDUAL graphic (a pad)
// instead of its whole layer. QtSvg still renders each leaf correctly, so the nested copper0>copper1
// case that broke the old split renderer is still handled -- we just also address its children.

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
		if (ev->buttons() & Qt::LeftButton) {
			m_editor->handleMove(mapToScene(ev->pos()));
			ev->accept();
			return;
		}
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

	m_svgLabel = new QLabel(this);
	m_svgLabel->setTextFormat(Qt::PlainText);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->addWidget(m_view, 1);
	m_layout->addWidget(m_svgLabel);

	m_renderer = nullptr;
}

PESvgEditor::~PESvgEditor()
{
	delete m_renderer;   // items reference but do not own it; destroy after the scene.
	m_renderer = nullptr;
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

QDomElement PESvgEditor::selectedElement() const
{
	if (m_selectedId.isEmpty()) return QDomElement();
	return PEPrimGeom::findById(m_doc.documentElement(), m_selectedId);
}
// Collect the addressable graphics to render as items: a graphic is addressable when it has an id
// AND a computable local bounds (so it can be hit-tested + moved). Layer groups are descended into;
// a graphic that happens to contain children (rare) is treated as an opaque leaf and rendered whole
// by its id. `visibleLayers` (when non-empty) restricts which top-level layer groups are considered.
void PESvgEditor::rebuildScene()
{
	m_scene->clear();                 // deletes items AND m_selection (they are scene items)
	m_selection = nullptr;
	delete m_renderer;
	m_renderer = nullptr;

	if (m_doc.isNull()) {
		return;
	}

	const QString svg = m_doc.toString();
	m_renderer = new QSvgRenderer(svg.toUtf8());
	if (!m_renderer->isValid()) {
		qWarning() << "PESvgEditor: QSvgRenderer could not parse the part SVG";
		return;
	}

	const QDomElement root = m_doc.documentElement();

	// Recursive descent: for each element under a (visible) layer group, if it is a modelled graphic
	// with id + bounds, make it an item and do NOT descend further (render it whole). Otherwise
	// (a group, or a container) descend. This yields exactly the leaves the editor can select/move.
	std::function<void(const QDomElement &)> walk = [&](const QDomElement & e) {
		for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
			const QString id = c.attribute(QStringLiteral("id"));
			const bool graphic = PEPrimGeom::kindFor(c) != PEPrimGeom::Kind::Group
			                     && PEPrimGeom::kindFor(c) != PEPrimGeom::Kind::Unknown;
			if (!id.isEmpty() && graphic && PEPrimGeom::localBounds(c).isValid()
			    && m_renderer->elementExists(id)) {
				auto * item = new QGraphicsSvgItem();
				item->setSharedRenderer(m_renderer);
				item->setElementId(id);
				// QGraphicsSvgItem renders setElementId(id) at the item's LOCAL origin, filling a
				// boundingRect() of size boundsOnElement(id) anchored at (0,0). So to place each leaf at
				// its true document position (and give hit-testing / the outline a matching scene rect),
				// move the item to boundsOnElement's top-left. Without this every leaf paints at (0,0).
				const QRectF bb = m_renderer->boundsOnElement(id);
				if (bb.isValid()) item->setPos(bb.topLeft());
				item->setData(0, id);        // id carried on the item for hit-testing
				m_scene->addItem(item);
				continue;                    // rendered whole; its children are inside this item
			}
			walk(c);
		}
	};

	for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
		const QString id = e.attribute(QStringLiteral("id"));
		if (!m_visibleLayers.isEmpty() && !m_visibleLayers.contains(id)) {
			continue;   // layer filter (fzp-declared layers for the active view)
		}
		// Top-level layer group: descend into it. A graphic at top level (unusual) is handled inside.
		walk(e);
	}

	m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-8, -8, 8, 8));
	m_needInitialFit = true;
	if (m_view->viewport()->width() > 1 && m_view->viewport()->height() > 1) {
		fitToWindow();
	}
	// Re-assert selection outline if the selected id still resolves (survives a redo-driven reload).
	updateSelectionVisual();
}

void PESvgEditor::updateSelectionVisual()
{
	if (m_selection) {
		m_selection->setParentItem(nullptr);
		if (m_selection->scene()) m_selection->scene()->removeItem(m_selection);
		delete m_selection;
		m_selection = nullptr;
	}
	const QDomElement el = selectedElement();
	if (el.isNull()) return;

	QRectF b = PEPrimGeom::localBounds(el);
	if (!b.isValid()) return;

	// Parent the outline to the selected item so it inherits the same element transform and stays
	// glued to the shape even while the item is dragged (setPos on the item moves the outline too).
	QGraphicsItem * owner = nullptr;
	for (QGraphicsItem * it : m_scene->items()) {
		if (it->data(0).toString() == m_selectedId) { owner = it; break; }
	}

	QPen pen(QColor(0x2a, 0xa0, 0xff));
	pen.setWidth(0);
	pen.setStyle(Qt::DashLine);
	pen.setCosmetic(true);
	m_selection = new QGraphicsRectItem(b);
	m_selection->setPen(pen);
	m_selection->setBrush(Qt::NoBrush);
	m_selection->setFlag(QGraphicsItem::ItemIsMovable, false);
	if (owner) {
		m_selection->setParentItem(owner);
	} else {
		m_selection->setZValue(1000);
		m_scene->addItem(m_selection);
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

// Hit-test the topmost leaf item whose local bounds contain the scene point. Items carry their
// element id under data(0); because each renders its element at its own local coordinates with the
// item placed at scene origin, the element's local bounds == its scene bounds.
static QString hitTestId(QGraphicsScene * scene, const QPointF & scenePos)
{
	// QGraphicsSvgItem::boundingRect() is the element's bounds in ITEM-LOCAL space (origin at the
	// element's own top-left), and each item now carries the element's ancestor transform, so
	// item->mapFromScene + boundingRect is an exact local hit-test. Among every id-bearing item that
	// contains the point, the SMALLEST-area one is the most specific (a pad drawn over a bigger
	// mounting-hole box / layer background), so pick that -- this is what makes clicking a pad select
	// the pad, not the hole or the layer it sits in.
	QString best;
	qreal bestArea = 1e30;
	const QList<QGraphicsItem *> candidates = scene->items();
	for (QGraphicsItem * it : candidates) {
		if (it->data(0).isNull()) continue;              // skip selection outline etc.
		const QPointF local = it->mapFromScene(scenePos);
		if (!it->boundingRect().contains(local)) continue;
		const qreal area = it->boundingRect().width() * it->boundingRect().height();
		if (area < bestArea) { bestArea = area; best = it->data(0).toString(); }
	}
	return best;
}

void PESvgEditor::handlePress(const QPointF & scenePos)
{
	const QString id = hitTestId(m_scene, scenePos);
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

	// Remember the item's resting position (== its boundsOnElement top-left, set in rebuildScene) so
	// the live preview can offset from it instead of replacing it. Without this a drag would teleport
	// the item back toward the origin, since setPos() is absolute, not relative.
	m_itemBasePos = QPointF(0, 0);
	for (QGraphicsItem * it : m_scene->items()) {
		if (it->data(0).toString() == id) { m_itemBasePos = it->pos(); break; }
	}

	const QDomElement el = selectedElement();
	const QRectF b = PEPrimGeom::localBounds(el);
	emit elementSelected(id);
	emit selectionInfo(QStringLiteral("%1 · %2 · (%3, %4) %5×%6")
		.arg(id, PEPrimGeom::kindName(PEPrimGeom::kindFor(el)))
		.arg(b.x(), 0, 'g', 5).arg(b.y(), 0, 'g', 5)
		.arg(b.width(), 0, 'g', 5).arg(b.height(), 0, 'g', 5));
	updateSelectionVisual();
}

void PESvgEditor::handleMove(const QPointF & scenePos)
{
	if (!m_dragging || m_selectedId.isEmpty()) return;

	m_dragDeltaScene = scenePos - m_dragSceneStart;

	// Live preview: move the item (and, because the outline is parented to it, the outline too) by
	// adding the scene-space delta to the item's resting position. The DOM is untouched until release
	// so we never re-serialize per pixel.
	QGraphicsItem * item = nullptr;
	for (QGraphicsItem * it : m_scene->items()) {
		if (it->data(0).toString() == m_selectedId) { item = it; break; }
	}
	if (item) item->setPos(m_itemBasePos + m_dragDeltaScene);
}

void PESvgEditor::handleRelease(const QPointF & scenePos)
{
	if (!m_dragging) return;
	m_dragging = false;

	Q_UNUSED(scenePos);
	if (m_selectedId.isEmpty()) return;

	const QPointF sceneDelta = m_dragDeltaScene;
	if (qAbs(sceneDelta.x()) < 1e-6 && qAbs(sceneDelta.y()) < 1e-6) {
		// No real drag: leave the item where it was (it never moved).
		return;
	}

	// The DOM geometry lives in the element's LOCAL user-unit space, which is the selected item's
	// own coordinate space (item->setTransform was set to transformForElement(id), and the view adds
	// fitInView's scale on top). Map the accumulated scene-space drag delta straight into that local
	// space via the item's scene transform -- this is exact for any nesting/scale, unlike dividing by
	// the view scale alone (which ignores the element's ancestor transform and the viewBox scale).
	QGraphicsItem * item = nullptr;
	for (QGraphicsItem * it : m_scene->items()) {
		if (it->data(0).toString() == m_selectedId) { item = it; break; }
	}
	if (!item) return;
	const QPointF localStart = item->mapFromScene(m_dragSceneStart);
	const QPointF localEnd   = item->mapFromScene(m_dragSceneStart + m_dragDeltaScene);
	QPointF userDelta = localEnd - localStart;

	// Restore the item to its resting position (undo the transient preview), then commit the move to
	// the DOM by id and reload so the render is exactly the file again.
	item->setPos(m_itemBasePos);
	QDomElement el = selectedElement();
	if (el.isNull()) return;
	if (!PEPrimGeom::moveBy(el, userDelta)) {
		// Non-translatable (e.g. <path> for now): snap the preview back, no-op.
		updateSelectionVisual();
		return;
	}

	MoveUndo mu{ m_selectedId, userDelta };
	// Drop any redo tail, push, advance.
	m_undo.resize(m_undoPos);
	m_undo.append(mu);
	m_undoPos = m_undo.size();
	m_dirty = true;
	emit dirtyChanged(true);
	emit undoRedoChanged();

	applyDocToRenderer();   // re-render from the committed document
}

void PESvgEditor::commitDrag()
{
	// Reserved: resize commits will use this alongside moveBy/resizeRect. Move commits inline in
	// handleRelease() because it needs the view scale to map the delta.
}

void PESvgEditor::applyDocToRenderer()
{
	// Reload the shared renderer from the (now edited) document without disturbing selection.
	const QString keepSel = m_selectedId;
	if (m_renderer) {
		m_renderer->load(m_doc.toString().toUtf8());
	}
	// Re-apply each leaf's position from the reloaded renderer: after a move the element's
	// boundsOnElement changed, so its item must move to the new top-left. Clearing then resetting the
	// element id forces QGraphicsSvgItem to re-render the changed geometry.
	const QList<QGraphicsItem *> all = m_scene->items();
	for (QGraphicsItem * it : all) {
		if (it->data(0).isNull()) continue;
		auto * svg = dynamic_cast<QGraphicsSvgItem *>(it);
		if (!svg) continue;
		const QString id = it->data(0).toString();
		svg->setElementId(QString());        // clear then reset forces a re-render of the new geometry
		svg->setElementId(id);
		const QRectF bb = m_renderer ? m_renderer->boundsOnElement(id) : QRectF();
		svg->setPos(bb.isValid() ? bb.topLeft() : QPointF(0, 0));
	}
	m_selectedId = keepSel;
	updateSelectionVisual();
}

void PESvgEditor::undo()
{
	if (!canUndo()) return;
	const MoveUndo mu = m_undo.at(m_undoPos - 1);
	m_undoPos--;
	QDomElement el = PEPrimGeom::findById(m_doc.documentElement(), mu.id);
	if (!el.isNull()) PEPrimGeom::moveBy(el, -mu.delta);
	m_selectedId = mu.id;
	m_dirty = true;
	emit undoRedoChanged();
	applyDocToRenderer();
}

void PESvgEditor::redo()
{
	if (!canRedo()) return;
	const MoveUndo mu = m_undo.at(m_undoPos);
	m_undoPos++;
	QDomElement el = PEPrimGeom::findById(m_doc.documentElement(), mu.id);
	if (!el.isNull()) PEPrimGeom::moveBy(el, mu.delta);
	m_selectedId = mu.id;
	m_dirty = true;
	emit undoRedoChanged();
	applyDocToRenderer();
}


