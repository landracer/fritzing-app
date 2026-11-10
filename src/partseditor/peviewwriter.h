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

#ifndef PEVIEWWRITER_H
#define PEVIEWWRITER_H

#include <QDomDocument>
#include <QString>

// DOM-writer for the sole-renderer Parts Editor (docs/design/pe-integration-plan.md §4).
//
// The single source of truth for a view's graphic is the QDomDocument the PESvgEditor edits in place.
// On save we must serialize *that same tree* straight to the view .svg. This class owns that last hop:
// normalize to Fritzing's SVG namespace convention (TextUtils::svgNSOnly -- drop the inherited
// xmlns:svg alias, guarantee xmlns="http://www.w3.org/2000/svg" on the root) and write the bytes
// atomically via QSaveFile, so a crash mid-write can never leave a half-written part graphic on disk.
//
// Deliberately decoupled from PESvgEditor/PEGraphicsItem: it takes a QDomDocument and a path and nothing
// else, so the serialization is golden-testable headless (tests/auto/test_peviewwriter) without the
// widget/render stack. It writes whatever it is handed -- the *un-colourised* doc, per the integration
// plan's "colourisation is a view-time transform, never written to disk" rule (see pesvgpartloader).
namespace PEViewWriter {

// Serialize @p doc to @p path with svgNSOnly normalization, atomically (QSaveFile). Returns false (and
// touches nothing durable) if the document is empty or the write fails.
bool writeViewSvg(const QDomDocument & doc, const QString & path);

// The exact on-disk text writeViewSvg produces for @p doc (svgNSOnly(doc.toString(4))). Exposed so the
// round-trip golden pins the byte convention independently of the file system.
QString normalizedSvg(const QDomDocument & doc);

} // namespace PEViewWriter

#endif // PEVIEWWRITER_H
