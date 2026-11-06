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

	// Resize grips. The marquee drawn for a selected interactive graphic gets eight draggable handles
	// (four corners + four edge midpoints). Geometry is kept in one place so paint, hit-test and drag
	// cannot drift apart the way the previous per-call coordinate maths did.
	// Bitmask of the marquee edges a held handle moves. Every value MUST be a distinct bit: an
	// earlier draft used GripLeft = 0, which made `grip & GripLeft` permanently false so left/top
	// drags silently did nothing. GripNone is the only non-bit value and is tested for explicitly.
	enum Grip {
		GripNone        = -1,
		GripLeft        = 1 << 0,
		GripRight       = 1 << 1,
		GripTop         = 1 << 2,
		GripBottom      = 1 << 3,
		GripTopLeft     = GripTop    | GripLeft,
		GripTopRight    = GripTop    | GripRight,
		GripBottomLeft  = GripBottom | GripLeft,
		GripBottomRight = GripBottom | GripRight
	};
	// The marquee rect in item-local pixels (inset by the same amount paint() insets it).
	QRectF marqueeRect();
	// The eight draggable handles as (mask, rect) pairs. Held as a table rather than an array indexed
	// by the mask, because corner masks are combinations (Top|Left) and would alias each other as
	// indices.
	struct GripHandle { int mask; QRectF rect; };
	QVector<GripHandle> resizeGrips();
	// Which grip mask the item-local pixel point p is on, GripNone if none.
	int hitResizeGrip(const QPointF & p);
	// Item-local pixels -> document user units, using the same doc<->px scale the grips are drawn with.
	QPointF pixelToDoc(const QPointF & p);
	// Apply a held grip dragged to item-local pixel p to the box captured at drag start.
	QRectF resizedBox(int grip, const QPointF & fromDoc, const QPointF & toDoc);
	void showPathAnchors(bool);
	bool showingPathAnchors();
	void setGrabbedAnchor(int);
	int grabbedAnchor();
	void setDocBounds(QRectF);
	QRectF docBounds();
	// M0.2: the absolute user-unit transform this element's local geometry resolves to (its own transform
	// times every ancestor <g>'s). Identity for the common case of a transform-free part SVG. Carried as
	// item state -- instead of baking the transform into the DOM, which used to corrupt the saved part --
	// so display, hit-testing and grip mapping can map local<->absolute without mutating the document.
	void setAbsoluteTransform(const QTransform & t);
	QTransform absoluteTransform() const { return m_absoluteTransform; }
	bool hasAbsoluteTransform() const { return !m_absoluteTransform.isIdentity(); }
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
	// Resize grips: a corner/edge handle of the selection marquee was dragged. 'to' is the element's
	// new bounding box in document user units; PEMainWindow applies it with resizeGraphic so the edit
	// is undoable. Before this there was no way to invoke resizeGraphic at all -- m_grabbedAnchor only
	// exists for <path> node editing, so rect/circle/line/ellipse had no resize affordance.
	void graphicResizeTo(PEGraphicsItem *, QRectF to);

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
	QTransform m_absoluteTransform; // M0.2: local->absolute user-unit mapping (identity when untransformed)
	bool m_drawHighlight = false;
	int m_wheelAccum = 0;
	qreal m_savedOpacity = 0;
	bool m_pick = false;
	bool m_interactive = false;   // fully-live slice: movable+selectable object (not a child overlay)
	QPointF m_bodyDragOrigin;     // scene px point where a body-drag started (empty = not dragging)
	bool m_draggingBody = false;  // true while the whole element is being dragged (vs a grip/anchor)

	// Resize grips. m_resizeGrip is a bitmask of the marquee edges/corners being dragged
	// (see GripXxx); -1 means no grip is held. m_resizeOriginDoc is the element bounding box at drag
	// start and m_resizeOriginPx the pointer's document-unit point at drag start, so the delta is
	// measured in user units rather than pixels and is therefore zoom-independent.
	int   m_resizeGrip = -1;
	bool  m_draggingResize = false;
	QRectF m_resizeOriginDoc;
	QPointF m_resizeOriginPx;
	class ItemBase * m_itemBase = nullptr;
};

#endif /* PEGRAPHICSITEM_H_ */
