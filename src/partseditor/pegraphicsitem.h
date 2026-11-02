/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

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



#ifndef PEGRAPHICSITEM_H_
#define PEGRAPHICSITEM_H_

#include <QGraphicsRectItem>
#include <QDomElement>
#include <QGraphicsSceneHoverEvent>
#include <QTimer>
#include <QPointer>
#include <QTime>
#include <QBrush>
#include <QVector>

class PEGraphicsItem : public QObject, public QGraphicsRectItem
{
	Q_OBJECT
public:
	PEGraphicsItem(double x, double y, double width, double height, class ItemBase *);
	~PEGraphicsItem();

	void hoverEnterEvent(QGraphicsSceneHoverEvent *);
	void hoverLeaveEvent(QGraphicsSceneHoverEvent *);
	void wheelEvent(QGraphicsSceneWheelEvent *);
	void mousePressEvent(QGraphicsSceneMouseEvent *);
	void mouseMoveEvent(QGraphicsSceneMouseEvent *);
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *);
	void setHighlighted(bool);
	bool highlighted();
	void setElement(QDomElement &);
	QDomElement & element();
	void setOffset(QPointF);
	QPointF offset();
	void showTerminalPoint(bool);
	bool showingTerminalPoint();
	void showMarquee(bool);
	bool showingMarquee();
	// P3 live path editing: draggable anchor (node) grips if this element is a <path>.
	int pathAnchorCount();
	QVector<QPointF> pathAnchors();          // document user-unit anchors, segment order
	void showPathAnchors(bool);
	bool showingPathAnchors();
	void setGrabbedAnchor(int);
	int grabbedAnchor();
	void setDocBounds(QRectF);
	QRectF docBounds();
	// Fully-live slice (docs/design/vector-editor.md §B.1): when interactive, this item becomes a
	// top-level movable+selectable object -- clicking focuses (selects) it and dragging its body moves
	// the underlying element (not the whole-view svg). Default off so non-interactive overlays behave
	// exactly as the legacy child-overlay did.
	void setInteractive(bool interactive);
	bool interactive() const { return m_interactive; }
	QPointF docOrigin() const { return m_docBounds.topLeft(); }   // element origin in user units
	void setTerminalPoint(QPointF);
	QPointF terminalPoint();
	void setPendingTerminalPoint(QPointF);
	QPointF pendingTerminalPoint();
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void setPickAppearance(bool);
	void flash();
	class ItemBase * itemBase();

Q_SIGNALS:
	void highlightSignal(PEGraphicsItem *);
	void mousePressedSignal(PEGraphicsItem *, bool & ignore);
	void mouseReleasedSignal(PEGraphicsItem *);
	void terminalPointMoved(PEGraphicsItem *, QPointF);
	void terminalPointChanged(PEGraphicsItem *, QPointF before, QPointF after);
	// P3 live path editing: emitted while/after a path anchor (node) is dragged. 'anchor'
	// is the document-unit anchor point (segmentIndex indexes pathAnchors()).
	void pathAnchorMoved(PEGraphicsItem *, int segmentIndex, QPointF anchor);
	void pathAnchorChanged(PEGraphicsItem *, int segmentIndex, QPointF before, QPointF after);
	// Fully-live slice: the whole (interactive) graphic was body-dragged; deltaUnits is the move in
	// document user units. PEMainWindow applies it to the element's geometry attrs (moveGraphic).
	void graphicMoveDelta(PEGraphicsItem *, QPointF deltaUnits);

protected Q_SLOTS:
	void restoreColor();

protected:
	bool m_highlighted = false;
	bool m_flash = false;
	QDomElement  m_element;
	QPointF m_offset;
	bool m_showTerminalPoint = false;
	bool m_showMarquee = false;
	QPointF m_terminalPoint;
	QPointF m_pendingTerminalPoint;
	bool m_dragTerminalPoint = false;
	QPointF m_dragTerminalOrigin;
	QPointF m_terminalPointOrigin;
	bool m_showPathAnchors = false;
	bool m_dragPathAnchor = false;
	int m_grabbedAnchor = -1;
	QPointF m_pathAnchorOrigin;     // document-unit anchor at drag start
	QPointF m_dragPathAnchorOrigin; // item-local pixel point at drag start
	QRectF m_docBounds;             // element bounds in document user units (for pixel<->doc mapping)
	bool m_drawHighlight = false;
	int m_wheelAccum = 0;
	qreal m_savedOpacity = 0;
	bool m_pick = false;
	bool m_interactive = false;   // fully-live slice: movable+selectable object (not a child overlay)
	QPointF m_bodyDragOrigin;     // scene px point where a body-drag started (empty = not dragging)
	bool m_draggingBody = false;  // true while the whole element is being dragged (vs a grip/anchor)
	class ItemBase * m_itemBase = nullptr;
};

#endif /* PEGRAPHICSITEM_H_ */
