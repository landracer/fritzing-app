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

#ifndef PEITEMPLAN_H
#define PEITEMPLAN_H

#include <QDomElement>
#include <QString>
#include <QStringList>
#include <QVector>

// The pure itemization decision that drives PESvgEditor::rebuildScene, kept free of the
// QGraphicsView/QSvgRenderer widget stack so it is headless-golden-testable (Qt Core/Xml only).
namespace PEItemPlan {

// One entry in the planned itemization: an element id and whether it is an editable leaf (carries
// data(0) -> selectable/movable) or an opaque group rendered whole (not selectable).
struct Planned {
	QString id;
	bool editable;
};

// Walk `root` once and return one Planned per disjoint subtree:
//   1. editable leaf (id'd primitive with bounds) -> {id, true}   (selectable/movable leaf)
//   2. group with NO editable graphic in subtree   -> {gid, false} (opaque art, renders whole, uneditable)
//   3. group WITH editable descendants             -> recurse (its editable children become rule-1 entries)
// Because a group is either rendered whole OR descended into (never both), each id appears at most once,
// so no element is painted twice -- the sole-renderer no-duplication guarantee. `visibleLayers`, when
// non-empty, restricts which top-level layer groups are considered (the fzp-declared layers for a view).
QVector<Planned> plan(const QDomElement & root, const QStringList & visibleLayers);

} // namespace PEItemPlan

#endif // PEITEMPLAN_H
