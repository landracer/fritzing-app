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

#ifndef PESELFPAINTITEM_H
#define PESELFPAINTITEM_H

#include <QGraphicsObject>
#include <QDomElement>
#include <QPainterPath>
#include <QRectF>
#include <QTransform>
#include <QVector>

// Slice A of docs/design/pe-selfpaint-renderer.md.
//
// The whole point of the sole-renderer is that ONE item draws ONE planned unit's geometry, and *nothing
// else*. QtSvg's QGraphicsSvgItem cannot do this: QSvgRenderer::render(painter, elementId) repaints the
// WHOLE document clipped to that element's bounding box, so a whole-group item and a leaf item that sits
// inside it both paint the leaf -> the PCB double-draw. This item self-paints instead: it decodes exactly
// the primitive(s) it owns (one for an editable leaf, every descendant for an opaque group) into styled
// QPainterPaths via PETransform::shapeFromElement, and paints only those. Because PEItemPlan guarantees each
// primitive belongs to exactly ONE planned unit, and each unit is exactly one of these items, no two items
// ever paint the same pixels -- a double-draw is structurally impossible.
//
// Geometry contract (matches PEDocument's M0.2 non-destructive model): each primitive's geometry attrs stay
// LOCAL; the transform it inherits from its ancestor <g>s is carried here and applied as this item's
// itemTransform, so it draws (and hit-tests) in the right scene place and a move maps through it.
class PESelfPaintItem : public QGraphicsObject
{
	Q_OBJECT

public:
	explicit PESelfPaintItem(QGraphicsItem * parent = nullptr);

	// The planned unit this item draws. The item's transform is always identity; the absolute placement is
	// computed inside recompute() by folding the unit's ancestor <g> transforms (and each primitive's own
	// 'transform') into each primitive's path, so geometry attrs stay local/unflattened (M0.2 non-destructive).
	void setElement(const QDomElement & element);

	// The id of the element this item addresses (empty for an item bound to a null element).
	QString id() const { return m_element.attribute(QStringLiteral("id")); }

	// Editable leaves are selectable; opaque (non-id'd art) groups are not. Set by the host per PEItemPlan.
	void setEditable(bool editable);
	bool editable() const { return m_editable; }

	// Combined hit shape in item-local coords (the union of this unit's primitive paths).
	QPainterPath hitShape() const;

	// The smallest-area hit-test used by PESvgEditor. Among every item that carries an id under data(0)
	// (i.e. every editable leaf) and whose boundingRect contains `point` (mapped into item-local space),
	// return the id of the SMALLEST-area one -- the most specific shape (a pad drawn over a bigger
	// mounting-hole box / layer background), so clicking a pad selects the pad, not the layer it sits in.
	// Returns empty when nothing id-bearing contains the point. Items are at identity transform with
	// absolute (flattened) geometry, so mapFromScene is exact and boundingRect is already scene-aligned.
	static QString hitTestId(const class QGraphicsScene * scene, const QPointF & point);

	// QGraphicsItem contract.
	QRectF boundingRect() const override;
	void paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget) override;

private:
	// One primitive this unit owns, with its decoded local path and the transform accumulated from the unit
	// element's ancestors down to it. Kept as a list (not merged) so each primitive paints with its OWN style.
	struct Sub { QDomElement element; QPainterPath path; QTransform transform; };

	// Decode m_element (a leaf primitive -> just it; a group -> every descendant primitive, document order).
	void recompute();

	QDomElement       m_element;
	QVector<Sub>      m_subs;        // decoded primitives owned by this unit (absolute mapping applied)
	QPainterPath      m_hit;         // union of m_subs paths (already in absolute scene coords)
	bool              m_editable = false;
};

#endif // PESELFPAINTITEM_H
