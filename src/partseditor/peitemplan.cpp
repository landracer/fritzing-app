// PEItemPlan -- the pure itemization decision behind PESvgEditor::rebuildScene (docs/design/
// pe-sole-renderer.md). No QGraphicsScene/QSvgRenderer: it operates purely on the QDomDocument tree,
// so the hybrid render model is golden-tested headlessly in tests/auto/test_pesvgeditor.
// SPDX-License-Identifier: GPL-3.0-or-later   Copyright (c) 2026 Fritzing

#include "peitemplan.h"
#include "peprimitivegeom.h"

#include <functional>

namespace PEItemPlan {

// A graphic element is *editable* when it has an id AND its kind is a modelled primitive with
// computable local bounds (rect/circle/ellipse/line/polyline/polygon). `<path>`/`<text>` are not
// yet editable (no localBounds / deferred path decode), and a `<g>` is a container, not a leaf.
static bool isEditableGraphic(const QDomElement & e)
{
	const PEPrimGeom::Kind k = PEPrimGeom::kindFor(e);
	const bool graphic = k != PEPrimGeom::Kind::Group && k != PEPrimGeom::Kind::Unknown;
	if (e.attribute(QStringLiteral("id")).isEmpty() || !graphic) return false;
	// A <path> is editable iff its anchors decode (node-editing works on them); localBounds is deferred for
	// paths, so use pathAnchors instead. Every other primitive is editable iff it has computable local bounds.
	if (k == PEPrimGeom::Kind::Path) return !PEPrimGeom::pathAnchors(e).isEmpty();
	return PEPrimGeom::localBounds(e).isValid();
}

// True when any descendant of `e` is an editable graphic (rule for "descend so the pad becomes its
// own item") -- i.e. whether `e`'s subtree should be decomposed to per-leaf items instead of painted
// whole. Breadboard's opaque art has no id'd primitive -> false -> whole-group item; pcb's copper group
// holds connector0pad -> true -> descend to the pad.
static bool subtreeHasEditableGraphic(const QDomElement & e)
{
	for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
		if (isEditableGraphic(c)) return true;
		if (subtreeHasEditableGraphic(c)) return true;
	}
	return false;
}

QVector<Planned> plan(const QDomElement & root, const QStringList & visibleLayers)
{
	QVector<Planned> out;

	// One step of the descent, applied at every nested level (the top level below calls it per root child):
	// for each child c,
	//   - an editable leaf primitive -> emit it (rule 1, selectable/movable);
	//   - a group with NO editable graphic in its subtree -> emit it whole (rule 2, opaque, uneditable);
	//   - otherwise (a group WITH editable descendants) -> descend into it (rule 3), so the pad inside
	//     becomes its own item rather than the group being painted whole over it.
	std::function<void(const QDomElement &)> walk = [&](const QDomElement & e) {
		for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
			if (isEditableGraphic(c)) { out.append({ c.attribute(QStringLiteral("id")), true }); continue; }
			if (PEPrimGeom::kindFor(c) == PEPrimGeom::Kind::Group
			    && !subtreeHasEditableGraphic(c)) {
				const QString gid = c.attribute(QStringLiteral("id"));
				if (!gid.isEmpty()) out.append({ gid, false });
				continue;
			}
			walk(c);
		}
	};

	// Top level: for each direct child of <svg>, apply the visibleLayers layer filter (non-empty -> only the
	// declared layers), then either emit an opaque whole-group unit, a top-level editable leaf, or descend.
	// `walk(root)` handles the descend/leaf/whole-group decision identically at every level; the only thing
	// special about the top level is the layer filter, which applies to a top-level group's id.
	for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
		const QString id = e.attribute(QStringLiteral("id"));
		if (!visibleLayers.isEmpty() && !id.isEmpty() && !visibleLayers.contains(id)) {
			continue;   // layer filter (fzp-declared layers for the active view)
		}
		if (isEditableGraphic(e)) { out.append({ id, true }); continue; }
		if (PEPrimGeom::kindFor(e) == PEPrimGeom::Kind::Group && !subtreeHasEditableGraphic(e)) {
			if (!id.isEmpty()) out.append({ id, false });
			continue;
		}
		// A top-level group WITH editable descendants: descend into it so its editable leaves become items.
		walk(e);
	}
	return out;
}

} // namespace PEItemPlan
