/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the GNU General Public License as published under the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#ifndef PESVGIMPORT_H
#define PESVGIMPORT_H

#include <QString>
#include <QDomDocument>
#include <QDomElement>
#include <QRectF>

// P6 (docs/design/vector-editor.md section F): the "import arbitrary SVG" half. A user picks any .svg
// (hand-drawn, exported from an editor, foreign); the part's own graphic group is whatever <g id> that
// document carries, which is arbitrary. These pure helpers turn such a foreign svg into the shape the
// editor consumes: a document whose root is an <svg> carrying exactly one graphic group <g id=layerName>
// (renamed from the source group id, or wrapped in a fresh one if the source had none), gorn-stripped and
// byte-stable on round-trip.
//
// The 'layerName' is the same graphic-group id convention as PEBlankPart ('breadboard'/'schematic'/..);
// the live caller passes the ViewLayer xml name. Pure DOM only -- no ViewLayer/QGraphicsItem.
namespace PESvgImport {

// Parse an svg source into 'doc'. Returns false on parse failure (caller keeps the old content).
bool parse(const QString & svg, QDomDocument & doc);

// Normalize a foreign svg document in place so the editor can treat it as a view graphic:
//   - ensure a single graphic group exists; if the source root already has exactly one <g id=..>
//     group, rename it to 'layerName' (returning the old id via outOldLayerName when non-null);
//   - if it has none, wrap all top-level graphic nodes in a fresh <g id=layerName>;
//   - strip 'gorn' attributes from every element (generic:origin convention is re-derived on save).
// Returns the (possibly new) graphic-group id. Idempotent: re-normalizing an already-normalized doc is a
// no-op on the group id.
QString normalize(QDomDocument & doc, const QString & layerName, QString * outOldLayerName = nullptr);

// Convenience: parse 'svg', normalize to 'layerName', and return the serialized document (byte-stable
// with the live editor's DOM->string). On parse failure returns the input unchanged.
QString normalizeToString(const QString & svg, const QString & layerName);

}   // namespace PESvgImport

#endif   // PESVGIMPORT_H
