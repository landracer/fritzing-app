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

#ifndef PECONNECTOR_H
#define PECONNECTOR_H

#include <QString>
#include <QDomElement>
#include <QDomDocument>

// P4 (docs/design/vector-editor.md section F): connector semantics authored in-app. A Fritzing
// part's connectors live in the FZP under <module><connectors><connector id=.. name=..>
// <views><viewName><p svgId=.. terminalId=.. /></viewName>..</views></connector>. The svg graphic
// a connector points at (its "pin" element) and the terminal point element are both identified by
// an svg id; the terminal handle is a node-edit special case (the <p terminalId> element). These
// are pure DOM builders/edits -- like the rest of the vector-editor model, inert until live wiring.
//
// 'viewName' is the fzp <view> key for a view (the caller passes ViewLayer::viewIDXmlName(viewID)),
// so this stays decoupled from the heavy ViewLayer/Qt-object machinery.

namespace PEConnector {

// Ensure the <connectors> container exists under the fzp module root and return it.
QDomElement ensureConnectors(QDomElement & fzpRoot);

// Author a new <connector id=connectorID name=name> block under fzpRoot's <connectors>, giving it a
// <viewName><p svgId=svgId .../></viewName> view block. Returns the connector element (or a null
// element on failure).
QDomElement authorConnector(QDomDocument & doc, QDomElement & fzpRoot,
                            const QString & connectorID, const QString & name,
                            const QString & viewName, const QString & svgId);

// Point an existing connector's per-view <p svgId> at a different graphic id. Returns true on success.
bool rebindGraphic(QDomElement & fzpConnector, const QString & viewName, const QString & newSvgId);

// Read the svg id a connector points at for a view (the pin graphic). Empty if absent.
QString graphicSvgId(const QDomElement & fzpConnector, const QString & viewName);

// The connector's terminal-point element id (the <p terminalId>), empty if none (centered).
QString terminalId(const QDomElement & fzpConnector, const QString & viewName);

}   // namespace PEConnector

#endif   // PECONNECTOR_H
