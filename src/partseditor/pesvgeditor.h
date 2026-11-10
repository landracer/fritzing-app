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
#include <QPointF>

class QGraphicsScene;
class QGraphicsView;
class QGraphicsSvgItem;
class QGraphicsRectItem;
class QSvgRenderer;
class QLabel;
class QVBoxLayout;

// The clean-parts-editor surface (docs/design/pe-sole-renderer.md).
//
// Sole renderer + editing surface. One QSvgRenderer over the colourised part SVG. The scene is
// decomposed to *addressable leaf* graphics: one QGraphicsSvgItem per editable element (pad, hole,
// track, ...) rather than per layer group, so an individual shape can be hit-tested and moved while
// the nested copper layer groups still render correctly as a whole. Each leaf item is positioned at
// renderer->boundsOnElement(id).topLeft() so its scene rect, hit-test and selection outline all agree
// with the painted geometry. Editing mutates the element's geometry attributes in the owned
// QDomDocument (by id), re-serializes, and reloads the renderer -- so the picture is always exactly
// what the document says. Selection shows a dashed outline (drawn from PEPrimGeom::localBounds,
// parented to the item so it tracks during a drag); dragging previews in scene space and on release
// maps the delta into the element's local user units (via the item's scene transform), commits the
// move to the DOM, and reloads. Undo/redo replays move deltas.
class PESvgEditor : public QWidget
{
	Q_OBJECT

public:
	explicit PESvgEditor(QWidget * parent = nullptr);
	~PESvgEditor() override;

	void setDocument(const QDomDocument & doc, const QString & svgLabel);
	void setVisibleLayers(const QStringList & layerGroupIds);
	void fitToWindow();

	// True when there is an undoable move committed.
	bool canUndo() const;
	bool canRedo() const;

	const QDomDocument & document() const { return m_doc; }
	QString currentFilepath() const { return m_filepath; }
	void setFilepath(const QString & path) { m_filepath = path; }

public Q_SLOTS:
	void undo();
	void redo();

Q_SIGNALS:
	void elementSelected(const QString & elementId);
	void selectionInfo(const QString & text);   // "id · kind · (x,y) w×h" for the status bar
	void dirtyChanged(bool dirty);
	void undoRedoChanged();

protected:
	void rebuildScene();
	void resizeEvent(QResizeEvent * event) override;

private:
	// Interaction lives in a QGraphicsView subclass so it sees mouse events in scene space and can
	// hit-test layer items to the sub-element under the cursor.
	friend class PESvgEditorView;
	void handlePress(const QPointF & scenePos);
	void handleMove(const QPointF & scenePos);
	void handleRelease(const QPointF & scenePos);

	QDomElement selectedElement() const;         // element for m_selectedId (null if none)
	void updateSelectionVisual();                // (re)draw the dashed outline over the selection
	void commitDrag();                           // push m_dragStart -> current onto the undo stack
	void applyDocToRenderer();                   // re-serialize m_doc and reload (used by undo/redo)

	struct MoveUndo { QString id; QPointF delta; };

	bool m_needInitialFit = false;

	QSvgRenderer * m_renderer = nullptr;
	QGraphicsScene * m_scene = nullptr;
	QGraphicsView * m_view = nullptr;
	QGraphicsRectItem * m_selection = nullptr;
	QLabel * m_svgLabel = nullptr;
	QVBoxLayout * m_layout = nullptr;
	QDomDocument m_doc;
	QString m_label;
	QString m_filepath;
	QStringList m_visibleLayers;

	// Selection / drag state.
	QString m_selectedId;
	bool m_dragging = false;
	QPointF m_dragSceneStart;      // scene pos at press
	QPointF m_dragDeltaScene;      // scene-space delta accumulated during drag
	QPointF m_itemBasePos;         // the selected item's pos() at press (its document placement)
	QVector<MoveUndo> m_undo;
	int m_undoPos = 0;             // number of committed moves that can be undone
	bool m_dirty = false;
};

#endif // PESVGEDITOR_H

