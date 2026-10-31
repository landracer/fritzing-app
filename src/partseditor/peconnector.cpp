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

#include "peconnector.h"

QDomElement PEConnector::ensureConnectors(QDomElement & fzpRoot)
{
    QDomElement connectors = fzpRoot.firstChildElement(QStringLiteral("connectors"));
    if (connectors.isNull()) {
        connectors = fzpRoot.ownerDocument().createElement(QStringLiteral("connectors"));
        fzpRoot.appendChild(connectors);
    }
    return connectors;
}

QDomElement PEConnector::authorConnector(QDomDocument & doc, QDomElement & fzpRoot,
                                         const QString & connectorID, const QString & name,
                                         const QString & viewName, const QString & svgId)
{
    if (connectorID.isEmpty() || svgId.isEmpty() || viewName.isEmpty()) return QDomElement();

    QDomElement connectors = ensureConnectors(fzpRoot);

    QDomElement connector = doc.createElement(QStringLiteral("connector"));
    connector.setAttribute(QStringLiteral("id"), connectorID);
    if (!name.isEmpty()) connector.setAttribute(QStringLiteral("name"), name);
    connectors.appendChild(connector);

    QDomElement views = doc.createElement(QStringLiteral("views"));
    connector.appendChild(views);

    QDomElement view = doc.createElement(viewName);
    views.appendChild(view);

    QDomElement p = doc.createElement(QStringLiteral("p"));
    p.setAttribute(QStringLiteral("svgId"), svgId);
    view.appendChild(p);

    return connector;
}

bool PEConnector::rebindGraphic(QDomElement & fzpConnector, const QString & viewName, const QString & newSvgId)
{
    if (newSvgId.isEmpty() || viewName.isEmpty()) return false;
    QDomElement views = fzpConnector.firstChildElement(QStringLiteral("views"));
    if (views.isNull()) return false;
    QDomElement view = views.firstChildElement(viewName);
    if (view.isNull()) return false;
    QDomElement p = view.firstChildElement(QStringLiteral("p"));
    if (p.isNull()) return false;
    p.setAttribute(QStringLiteral("svgId"), newSvgId);
    return true;
}

QString PEConnector::graphicSvgId(const QDomElement & fzpConnector, const QString & viewName)
{
    QDomElement views = fzpConnector.firstChildElement(QStringLiteral("views"));
    if (views.isNull()) return QString();
    QDomElement view = views.firstChildElement(viewName);
    if (view.isNull()) return QString();
    QDomElement p = view.firstChildElement(QStringLiteral("p"));
    if (p.isNull()) return QString();
    return p.attribute(QStringLiteral("svgId"));
}

QString PEConnector::terminalId(const QDomElement & fzpConnector, const QString & viewName)
{
    QDomElement views = fzpConnector.firstChildElement(QStringLiteral("views"));
    if (views.isNull()) return QString();
    QDomElement view = views.firstChildElement(viewName);
    if (view.isNull()) return QString();
    QDomElement p = view.firstChildElement(QStringLiteral("p"));
    if (p.isNull()) return QString();
    return p.attribute(QStringLiteral("terminalId"));
}

