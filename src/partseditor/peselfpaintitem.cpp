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

#include "peselfpaintitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsItem>

#include "petransform.h"

// ---- style decode (the raw fill/stroke -> brush/pen logic PEGraphicsItem used, ItemBase-free) ----------

static Qt::FillRule fillRuleOf(const QDomElement & el) {
	const QString fr = el.attribute(QStringLiteral("fill-rule"));
	if (fr.compare(QStringLiteral("evenodd"), Qt::CaseInsensitive) == 0) return Qt::OddEvenFill;
	return Qt::WindingFill;
}

static void paintOne(QPainter * p, const QDomElement & el, const QPainterPath & path) {
	const QString stroke = el.attribute(QStringLiteral("stroke"));
	bool sok = false;
	const double swv = el.attribute(QStringLiteral("stroke-width")).toDouble(&sok);
	QPen pen;
	if (!stroke.isEmpty() && stroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
		pen.setColor(QColor(stroke));
		pen.setWidthF(sok ? swv : 0.0);
	} else {
		pen.setStyle(Qt::NoPen);
	}
	const QString fill = el.attribute(QStringLiteral("fill"));
	QBrush brush;
	if (fill.isEmpty() || fill.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
		brush.setStyle(Qt::NoBrush);
	} else {
		brush = QBrush(QColor(fill));
		brush.setStyle(Qt::SolidPattern);
	}
	QPainterPath styled = path;
	styled.setFillRule(fillRuleOf(el));
	p->setPen(pen);
	p->setBrush(brush);
	p->drawPath(styled);
}

// ---- PESelfPaintItem ---------------------------------------------------------------------------------

PESelfPaintItem::PESelfPaintItem(QGraphicsItem * parent)
	: QGraphicsObject()
{
	Q_UNUSED(parent)   // host adds us to the scene; kept in the signature for clarity
	setFlag(QGraphicsItem::ItemIsSelectable, false);
}

void PESelfPaintItem::setElement(const QDomElement & element)
{
	m_element = element;
	recompute();
}

void PESelfPaintItem::setEditable(bool editable)
{
	m_editable = editable;
	setFlag(QGraphicsItem::ItemIsSelectable, editable);
}

void PESelfPaintItem::recompute()
{
	prepareGeometryChange();
	m_subs.clear();
	m_hit = QPainterPath();
	if (m_element.isNull()) return;

	// The host (PESvgEditor::rebuildScene) flattens every primitive's inherited transform into its
	// geometry attrs (PETransform::flatten, idempotent) BEFORE binding it here, so all geometry attrs are
	// already absolute and there is no 'transform' left to compose. Thus every primitive we own decodes at
	// the identity transform and the item transform is identity -- the decoded local path IS absolute.
	QVector<QDomElement> work;
	work.append(m_element);
	while (!work.isEmpty()) {
		const QDomElement ce = work.takeFirst();
		QPainterPath local;
		if (PETransform::shapeFromElement(ce, local)) {
			Sub s; s.element = ce; s.path = local; s.transform = QTransform();
			m_subs.append(s);
			m_hit.addPath(local);
		} else {
			// A container (<g>): descend in document order (no transform -- already absorbed by flatten).
			for (QDomElement c = ce.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
				work.append(c);
		}
	}
}

QPainterPath PESelfPaintItem::hitShape() const
{
	return m_hit;
}

// Smallest-area id-bearing hit-test. See the header for the contract. Lives here (not in PESvgEditor) so it
// is golden-testable headless against a scene of PESelfPaintItems, and reused verbatim by the editor.
QString PESelfPaintItem::hitTestId(const QGraphicsScene * scene, const QPointF & point)
{
	QString best;
	qreal bestArea = 1e30;
	if (!scene) return best;
	const QList<QGraphicsItem *> candidates = scene->items();
	for (QGraphicsItem * it : candidates) {
		if (it->data(0).isNull()) continue;              // skip opaque groups + selection outline
		const QPointF local = it->mapFromScene(point);
		if (!it->boundingRect().contains(local)) continue;
		const qreal area = it->boundingRect().width() * it->boundingRect().height();
		if (area < bestArea) { bestArea = area; best = it->data(0).toString(); }
	}
	return best;
}

QRectF PESelfPaintItem::boundingRect() const
{
	if (m_hit.isEmpty()) return QRectF();
	// Expand by half the widest stroke among our primitives so a thin stroke isn't clipped.
	double halfStroke = 0.0;
	for (const Sub & s : m_subs) {
		const QString sw = s.element.attribute(QStringLiteral("stroke-width"));
		if (!sw.isEmpty()) halfStroke = qMax(halfStroke, sw.toDouble() / 2.0);
	}
	return m_hit.boundingRect().adjusted(-halfStroke, -halfStroke, halfStroke, halfStroke);
}

void PESelfPaintItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	Q_UNUSED(option)
	Q_UNUSED(widget)
	if (m_subs.isEmpty()) return;

	painter->setRenderHint(QPainter::Antialiasing, true);
	// Paint each primitive we own with its own style, in document order. Because PEItemPlan assigned each
	// primitive to exactly one unit, and this item is that unit, this is the ONLY paint those pixels get.
	for (const Sub & s : m_subs)
		paintOne(painter, s.element, s.path * s.transform);
}
