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


#include "pegraphicsitem.h"
#include "../debugdialog.h"
#include "../sketch/infographicsview.h"
#include "../items/itembase.h"
#include "pedocument.h"   // PEElement/PEElementKind (typed path anchors)
#include "petransform.h"  // P10: shapeFromElement (live self-render of the element geometry)

#include <QBrush>
#include <QColor>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>


static QVector<qreal> Dashes;
static constexpr int DashLength = 3;

static bool ShiftDown = false;
static QPointF OriginalShiftPos;
static bool ShiftX = false;
static bool ShiftY = false;
static constexpr double MinMouseMove = 2;
static const QColor NormalColor(0, 0, 255);
static const QColor PickColor(255, 0, 255);

// Resolve the element's fill-rule attribute into a Qt fill rule (default: nonzero winding).
static Qt::FillRule fillRuleOf(const QDomElement & el) {
    const QString fr = el.attribute(QStringLiteral("fill-rule"));
    if (fr.compare(QStringLiteral("evenodd"), Qt::CaseInsensitive) == 0) return Qt::OddEvenFill;
    return Qt::WindingFill;
}


////////////////////////////////////////////////


PEGraphicsItem::PEGraphicsItem(double x, double y, double w, double h, ItemBase * itemBase) : QGraphicsRectItem(x, y, w, h) {
	if (Dashes.isEmpty()) {
		Dashes << DashLength << DashLength;
	}

	m_itemBase = itemBase;
	m_pick = m_flash = false;

	m_terminalPoint = QPointF(w / 2, h / 2);
	m_dragTerminalPoint = m_showMarquee = m_showTerminalPoint = false;
	setAcceptedMouseButtons(Qt::LeftButton);
	setAcceptHoverEvents(true);
	//setFlag(QGraphicsItem::ItemIsSelectable, true );
	setHighlighted(false);
	setBrush(QBrush(NormalColor));
}

PEGraphicsItem::~PEGraphicsItem() {
	m_element.clear();
}

void PEGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *) {
	m_wheelAccum = 0;
	setHighlighted(true);
}

void PEGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *) {
	setHighlighted(false);
}

void PEGraphicsItem::wheelEvent(QGraphicsSceneWheelEvent * event) {
	//DebugDialog::debug(QString("wheel %1 %2").arg(event->delta()).arg(event->orientation()));

#ifndef Q_OS_MACOS
	// qt 4.8.3: mac: event orientation is messed up at this point
	if (event->orientation() == Qt::Horizontal) return;
#endif
	if (event->delta() == 0) return;
	if ((event->modifiers() & Qt::ShiftModifier) == 0) return;


	// delta one click forward = 120; delta one click backward = -120

	int magDelta = qAbs(event->delta());
	int sign = event->delta() / magDelta;
	int delta = sign * qMin(magDelta, 120);
	m_wheelAccum += delta;
	if (qAbs(m_wheelAccum) < 120) return;

	m_wheelAccum = 0;

	QList<PEGraphicsItem *> items;
	Q_FOREACH (QGraphicsItem * item, scene()->items(event->scenePos())) {
		auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
		if (pegi != nullptr) items.append(pegi);
	}

	if (items.count() < 2) return;

	int ix = -1;
	int mix = -1;
	for (int i = 0; i < items.count(); i++) {
		if (items.at(i)->highlighted()) {
			ix = i;
			break;
		}
		if (items.at(i) == this) {
			mix = i;
		}
	}

	if (ix == -1) ix = mix;
	if (ix == -1) {
		// shouldn't happen
		return;
	}

	ix += sign;

	// wrap
	//while (ix < 0) {
	//    ix += items.count();
	//}
	//ix = ix % items.count();

	// don't wrap
	if (ix < 0) ix = 0;
	else if (ix >= items.count()) ix = items.count() - 1;

	PEGraphicsItem * theItem = items.at(ix);
	if (theItem->highlighted()) {
		theItem->flash();
	}
	else {
		theItem->setHighlighted(true);
	}
}

void PEGraphicsItem::setHighlighted(bool highlighted) {
	if (highlighted) {
		m_highlighted = true;
		setOpacity(0.4);
		Q_FOREACH (QGraphicsItem * item, scene()->items()) {
			auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
			if (pegi == nullptr) continue;
			if (pegi == this) continue;
			if (!pegi->highlighted()) continue;

			pegi->setHighlighted(false);
		}
		Q_EMIT highlightSignal(this);
	}
	else {
		m_highlighted = false;
		setOpacity(0.001);
	}
	update();
}

bool PEGraphicsItem::highlighted() {
	return m_highlighted;
}

void PEGraphicsItem::setElement(QDomElement & el)
{
	m_element = el;
}

QDomElement & PEGraphicsItem::element() {
	return m_element;
}

void PEGraphicsItem::setOffset(QPointF p) {
	m_offset = p;
}

QPointF PEGraphicsItem::offset() {
	return m_offset;
}

void PEGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);

	// P10 (docs/design/vector-editor.md F.5): the live item self-renders the element's own geometry
	// (fill + stroke) instead of painting a baked fill over a static raster. The rendered path and the
	// hit-test shape both come from PETransform::shapeFromElement(m_element) -- the same attrs the
	// command layer edits -- so display always equals model with no separate renderer coordinate space.
	if (!m_element.isNull()) {
		QPainterPath shape;
		if (PETransform::shapeFromElement(m_element, shape)) {
			shape.setFillRule(fillRuleOf(m_element));
			const QString stroke = m_element.attribute(QStringLiteral("stroke"));
			bool sok = false;
			double swv = m_element.attribute(QStringLiteral("stroke-width")).toDouble(&sok);
			QPen pen;
			if (!stroke.isEmpty() && stroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
				pen.setColor(QColor(stroke));
				pen.setWidthF(sok ? swv : 0.0);
			} else {
				pen.setStyle(Qt::NoPen);
			}
			const QString fill = m_element.attribute(QStringLiteral("fill"));
			QBrush brush;
			if (fill.isEmpty() || fill.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
				brush.setStyle(Qt::NoBrush);
			} else {
				brush = QBrush(QColor(fill));
				brush.setStyle(Qt::SolidPattern);
			}
			painter->setPen(pen);
			painter->setBrush(brush);
			painter->drawPath(shape);
		}
	}

	if (m_flash) {
		m_flash = false;
		QTimer::singleShot(40, this, SLOT(restoreColor()));
	}

	bool save = m_showTerminalPoint || m_showMarquee || m_showPathAnchors;

	if (save) painter->save();

	if (m_showPathAnchors) {
		QRectF r = rect();
		QRectF db = m_docBounds;
		if (db.width() > 0 && db.height() > 0) {
			double sx = r.width() / db.width();
			double sy = r.height() / db.height();
			qreal side = qMin(r.width(), r.height()) / 10.0;
			if (side <= 0) side = 4;
			QVector<QPointF> anchors = pathAnchors();
			for (int i = 0; i < anchors.count(); ++i) {
				QPointF a = anchors.at(i);
				// document units -> item-local pixel coords
				qreal px = (a.x() - db.left()) * sx;
				qreal py = (a.y() - db.top()) * sy;
				QRectF grip(px - side / 2, py - side / 2, side, side);
				painter->setOpacity(1.0);
				painter->setPen(QPen(QColor(0, 0, 0), 0, Qt::SolidLine));
				painter->setBrush(i == m_grabbedAnchor ? QColor(255, 0, 255) : Qt::NoBrush);
				painter->drawRect(grip);
			}
		}
	}

	if (m_showTerminalPoint) {
		QRectF r = rect();
		QLineF l1(0, m_terminalPoint.y(), r.width(), m_terminalPoint.y());
		QLineF l2(m_terminalPoint.x(), 0, m_terminalPoint.x(), r.height());

		painter->setOpacity(1.0);
		painter->setPen(QPen(QColor(0, 0, 0), 0, Qt::SolidLine));
		painter->setBrush(Qt::NoBrush);
		painter->drawLine(l1);
		painter->drawLine(l2);

		painter->setPen(QPen(QColor(255, 255, 255), 0, Qt::DashLine));
		painter->setBrush(Qt::NoBrush);
		painter->drawLine(l1);
		painter->drawLine(l2);
	}

	if (m_showMarquee) {
		QRectF r = rect();
		double d = qMin(r.width(), r.height()) / 16;
		r.adjust(d, d, -d, -d);

		painter->setOpacity(1.0);
		painter->setPen(QPen(QColor(0, 0, 0), 0, Qt::SolidLine));
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(r);

		painter->setPen(QPen(QColor(255, 255, 255), 0, Qt::DashLine));
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(r);
	}

	if (save) painter->restore();
}

void PEGraphicsItem::showTerminalPoint(bool show)
{
	m_showTerminalPoint = show;
	update();
}

// Fully-live slice (vector-editor.md §B.1): when interactive, this item is a standalone movable +
// focusable object. A click focuses it (select -> highlight shows its grips); Qt's built-in movable
// drag then relocates it, which PEMainWindow maps back onto the element's geometry attrs. Non-interactive
// items keep the legacy child-overlay behavior untouched.
void PEGraphicsItem::setInteractive(bool interactive) {
	m_interactive = interactive;
	if (interactive) {
		setFlag(QGraphicsItem::ItemIsMovable, true);
		setFlag(QGraphicsItem::ItemIsFocusable, true);
		setFlag(QGraphicsItem::ItemIsSelectable, true);
		showMarquee(true);
		if (m_element.tagName().compare(QStringLiteral("path")) == 0) showPathAnchors(true);
	}
}


bool PEGraphicsItem::showingTerminalPoint() {
	return m_showTerminalPoint;
}

void PEGraphicsItem::showMarquee(bool show) {
	if (show) {
		m_showMarquee = true;
		Q_FOREACH (QGraphicsItem * item, scene()->items()) {
			auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
			if (pegi == nullptr) continue;
			if (pegi == this) continue;
			if (!pegi->showingMarquee()) continue;

			pegi->showMarquee(false);
		}
	}
	else {
		m_showMarquee = false;
	}
	update();
}

bool PEGraphicsItem::showingMarquee() {
	return m_showMarquee;
}

// ---- P3 live path editing ----

int PEGraphicsItem::pathAnchorCount() {
	return pathAnchors().count();
}

QVector<QPointF> PEGraphicsItem::pathAnchors() {
	if (m_element.tagName().compare(QStringLiteral("path")) != 0) return QVector<QPointF>();
	PEElement pe;
	pe.kind = PEElementKind::Path;
	pe.element = m_element;
	QList<QPointF> anchors = pe.pathAnchors();
	QVector<QPointF> out;
	for (const QPointF & a : anchors) out.append(a);
	return out;
}

void PEGraphicsItem::showPathAnchors(bool show) {
	m_showPathAnchors = show;
	update();
}

bool PEGraphicsItem::showingPathAnchors() {
	return m_showPathAnchors;
}

void PEGraphicsItem::setGrabbedAnchor(int i) {
	m_grabbedAnchor = i;
}

int PEGraphicsItem::grabbedAnchor() {
	return m_grabbedAnchor;
}

void PEGraphicsItem::setDocBounds(QRectF r) {
	m_docBounds = r;
}

// M0.2: carry the local->absolute mapping as item state. Setting a non-identity transform changes where
// the geometry actually sits, so invalidate the item's shape to make Qt re-derive its bounding region;
// paint() and the grip mapping read m_absoluteTransform directly.
void PEGraphicsItem::setAbsoluteTransform(const QTransform & t) {
	if (m_absoluteTransform == t) return;
	m_absoluteTransform = t;
	prepareGeometryChange();
}

QRectF PEGraphicsItem::docBounds() {
	return m_docBounds;
}

void PEGraphicsItem::setTerminalPoint(QPointF p) {
	m_pendingTerminalPoint = m_terminalPoint = p;
}

QPointF PEGraphicsItem::terminalPoint() {
	return m_terminalPoint;
}

void PEGraphicsItem::setPendingTerminalPoint(QPointF p) {
	m_pendingTerminalPoint = p;
}

QPointF PEGraphicsItem::pendingTerminalPoint() {
	return m_pendingTerminalPoint;
}

void PEGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent * event) {
	m_dragTerminalPoint = false;

	if (!m_interactive && !m_highlighted) {
		// legacy overlay: a non-highlighted (unselected) overlay click-throughs to the next layer
		event->ignore();
		return;
	}

	if (!event->buttons() && (Qt::LeftButton != 0u)) {
		event->ignore();
		return;
	}

	InfoGraphicsView * infoGraphicsView = InfoGraphicsView::getInfoGraphicsView(this);
	if ((infoGraphicsView != nullptr) && infoGraphicsView->spaceBarIsPressed()) {
		event->ignore();
		return;
	}

	if (m_interactive) {
		// Fully-live slice: pressing a live graphic selects it (highlight -> grips appear). If the press
		// is on a path node/terminal grip, we take over for grip-drag; otherwise we accept (so Qt's built-in
		// movable drag relocates the item) and PEMainWindow converts the move into a geometry edit.
		Q_EMIT highlightSignal(this);
		QPointF p = event->pos();
		QVector<QPointF> anchors = pathAnchors();
		if (!anchors.isEmpty() && m_docBounds.width() > 0 && m_docBounds.height() > 0) {
			double sx = rect().width() / m_docBounds.width();
			double sy = rect().height() / m_docBounds.height();
			qreal side = qMin(rect().width(), rect().height()) / 10.0;
			if (side <= 0) side = 4;
			int hit = -1;
			qreal best = 1e30;
			for (int i = 0; i < anchors.count(); ++i) {
				QPointF a = anchors.at(i);
				qreal px = (a.x() - m_docBounds.left()) * sx;
				qreal py = (a.y() - m_docBounds.top()) * sy;
				QRectF grip(px - side / 2, py - side / 2, side, side);
				if (grip.adjusted(-2, -2, 2, 2).contains(p)) {
					qreal d = QLineF(p, QPointF(px, py)).length();
					if (d < best) { best = d; hit = i; }
				}
			}
			if (hit >= 0) {
				m_dragPathAnchor = true;
				m_grabbedAnchor = hit;
				m_pathAnchorOrigin = anchors.at(hit);
				m_dragPathAnchorOrigin = p;
				event->accept();
				return;
			}
		}
		if (qAbs(p.x() - m_terminalPoint.x()) <= MinMouseMove && qAbs(p.y() - m_terminalPoint.y()) <= MinMouseMove) {
			m_dragTerminalPoint = true;
			m_terminalPointOrigin = m_terminalPoint;
			m_dragTerminalOrigin = event->pos();
			event->accept();
			return;
		}
		// body drag: let the base movable-item drag relocate us
		m_draggingBody = true;
		m_bodyDragOrigin = event->scenePos();
		QGraphicsRectItem::mousePressEvent(event);
		return;
	}

	bool ignore;
	Q_EMIT mousePressedSignal(this, ignore);
	if (ignore) {
		event->ignore();
		return;
	}

	if (m_showMarquee) {
		QPointF p = event->pos();
		if (event->modifiers() & Qt::ShiftModifier) {
			ShiftDown = true;
			ShiftX = ShiftY = false;
			OriginalShiftPos = p;
		}

		// P3: a path element offers draggable anchors instead of a single terminal point.
		QVector<QPointF> anchors = pathAnchors();
		if (!anchors.isEmpty() && m_docBounds.width() > 0 && m_docBounds.height() > 0) {
			double sx = rect().width() / m_docBounds.width();
			double sy = rect().height() / m_docBounds.height();
			qreal side = qMin(rect().width(), rect().height()) / 10.0;
			if (side <= 0) side = 4;
			int hit = -1;
			qreal best = 1e30;
			for (int i = 0; i < anchors.count(); ++i) {
				QPointF a = anchors.at(i);
				qreal px = (a.x() - m_docBounds.left()) * sx;
				qreal py = (a.y() - m_docBounds.top()) * sy;
				QRectF grip(px - side / 2, py - side / 2, side, side);
				if (grip.adjusted(-2, -2, 2, 2).contains(p)) {
					qreal d = QLineF(p, QPointF(px, py)).length();
					if (d < best) { best = d; hit = i; }
				}
			}
			if (hit >= 0) {
				m_dragPathAnchor = true;
				m_grabbedAnchor = hit;
				m_pathAnchorOrigin = anchors.at(hit);
				m_dragPathAnchorOrigin = p;
				return;
			}
		}

		if (qAbs(p.x() - m_terminalPoint.x()) <= MinMouseMove && qAbs(p.y() - m_terminalPoint.y()) <= MinMouseMove) {
			m_dragTerminalPoint = true;
			m_terminalPointOrigin = m_terminalPoint;
			m_dragTerminalOrigin = event->pos();
		}

		return;
	}

	event->ignore();
}

void PEGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent * event) {
	if (m_dragPathAnchor) {
		if (m_docBounds.width() > 0 && m_docBounds.height() > 0) {
			double sx = rect().width() / m_docBounds.width();
			double sy = rect().height() / m_docBounds.height();
			QPointF p = event->pos();
			// item-local pixel -> document user units
			QPointF docPoint(m_docBounds.left() + p.x() / sx, m_docBounds.top() + p.y() / sy);
			Q_EMIT pathAnchorMoved(this, m_grabbedAnchor, docPoint);
		}
		update();
		return;
	}

	if (!m_dragTerminalPoint) return;

	if (ShiftDown && !(event->modifiers() & Qt::ShiftModifier)) {
		ShiftDown = false;
	}
	QPointF p = event->pos();
	if (ShiftDown) {
		if (ShiftX) {
			// moving along x, constrain y
			p.setY(OriginalShiftPos.y());
		}
		else if (ShiftY) {
			// moving along y, constrain x
			p.setX(OriginalShiftPos.x());
		}
		else {
			double dx = qAbs(p.x() - OriginalShiftPos.x());
			double dy = qAbs(p.y() - OriginalShiftPos.y());
			if (dx - dy > MinMouseMove) {
				ShiftX = true;
			}
			else if (dy - dx > MinMouseMove) {
				ShiftY = true;
			}
		}
	}
	else if (event->modifiers() & Qt::ShiftModifier) {
		ShiftDown = true;
		ShiftX = ShiftY = false;
		OriginalShiftPos = event->pos();
	}


	QPointF newTerminalPoint = m_terminalPointOrigin + p - m_dragTerminalOrigin;
	if (newTerminalPoint.x() < 0) newTerminalPoint.setX(0);
	else if (newTerminalPoint.x() > rect().width()) newTerminalPoint.setX(rect().width());
	if (newTerminalPoint.y() < 0) newTerminalPoint.setY(0);
	else if (newTerminalPoint.y() > rect().height()) newTerminalPoint.setY(rect().height());
	m_terminalPoint = newTerminalPoint;
	Q_EMIT terminalPointMoved(this, newTerminalPoint);
	update();
}


void PEGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent * event) {
	if (m_dragPathAnchor) {
		m_dragPathAnchor = false;
		QVector<QPointF> anchors = pathAnchors();
		QPointF after = (m_grabbedAnchor >= 0 && m_grabbedAnchor < anchors.count()) ? anchors.at(m_grabbedAnchor) : QPointF();
		if (m_pathAnchorOrigin != after) {
			Q_EMIT pathAnchorChanged(this, m_grabbedAnchor, m_pathAnchorOrigin, after);
		}
		m_grabbedAnchor = -1;
		return;
	}
	if (m_dragTerminalPoint) {
		m_dragTerminalPoint = false;
		if (m_terminalPointOrigin != m_terminalPoint) {
			Q_EMIT terminalPointChanged(this, m_terminalPointOrigin, m_terminalPoint);
		}
		return;
	}
	if (m_draggingBody) {
		// Fully-live slice: the whole graphic was body-dragged. Convert the on-screen (px) move into a
		// user-unit delta relative to the press point, and hand it to PEMainWindow via the same
		// mouseReleasedSignal the connector flow uses. PEMainWindow applies it with moveGraphic/
		// MoveGraphicCommand (which translates every kind's attrs, incl. <path>) so the edit is real+undoable.
		m_draggingBody = false;
		const QPointF deltaScene = event->scenePos() - m_bodyDragOrigin;
		m_bodyDragOrigin = QPointF();
		QRectF r = rect();
		const QRectF doc = m_docBounds;
		const double sux = (r.width()  > 0 && doc.width()  > 0) ? doc.width()  / r.width()  : 1.0;
		const double suy = (r.height() > 0 && doc.height() > 0) ? doc.height() / r.height() : 1.0;
		Q_EMIT mouseReleasedSignal(this);
		Q_EMIT graphicMoveDelta(this, QPointF(deltaScene.x() * sux, deltaScene.y() * suy));
		return;
	}
	// legacy overlay: relocate the connector
	Q_EMIT mouseReleasedSignal(this);
}

void PEGraphicsItem::setPickAppearance(bool pick) {
	m_pick = pick;
	setBrush(pick ? PickColor : NormalColor);
}

void PEGraphicsItem::flash() {
	m_savedOpacity = opacity();
	m_flash = true;
	setBrush(QColor(255, 255, 255));
	update();
}

void PEGraphicsItem::restoreColor() {
	setBrush(m_pick ? PickColor : NormalColor);
	setOpacity(m_savedOpacity);
	update();
}

ItemBase * PEGraphicsItem::itemBase() {
	return m_itemBase;
}
