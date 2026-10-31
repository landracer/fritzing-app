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

#include "pesvgimport.h"

static void stripGorn(QDomElement & element)
{
    if (element.isNull()) return;
    if (element.hasAttribute(QStringLiteral("gorn")))
        element.removeAttribute(QStringLiteral("gorn"));
    QDomElement child = element.firstChildElement();
    while (!child.isNull()) {
        stripGorn(child);
        child = child.nextSiblingElement();
    }
}

bool PESvgImport::parse(const QString & svg, QDomDocument & doc)
{
    QDomDocument::ParseResult result =
        doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
    return static_cast<bool>(result);
}

QString PESvgImport::normalize(QDomDocument & doc, const QString & layerName, QString * outOldLayerName)
{
    QDomElement root = doc.documentElement();
    if (root.isNull()) return QString();

    // Collect top-level graphic groups/elements (everything except <defs>/<title>/<metadata>-style helpers
    // we don't want inside the graphic group; here we treat every child element as content).
    QDomElement singleGroup;
    int groupCount = 0;
    QDomElement firstChild;
    QDomElement el = root.firstChildElement();
    while (!el.isNull()) {
        firstChild = el;   // remember any first element (for the wrap case)
        if (el.tagName().compare(QStringLiteral("g"), Qt::CaseInsensitive) == 0) {
            ++groupCount;
            if (singleGroup.isNull()) singleGroup = el;
        }
        el = el.nextSiblingElement();
    }

    QString resultId = layerName;

    if (groupCount == 1 && !singleGroup.isNull()) {
        // Already exactly one group: rename it to the canonical layer id.
        QString oldId = singleGroup.attribute(QStringLiteral("id"));
        if (outOldLayerName != nullptr) *outOldLayerName = oldId;
        singleGroup.setAttribute(QStringLiteral("id"), layerName);
        resultId = layerName;
    } else {
        // No group (or more than one): wrap every top-level element into a fresh <g id=layerName> in
        // document order so the editor has one graphic group to draw into.
        QDomElement group = doc.createElement(QStringLiteral("g"));
        group.setAttribute(QStringLiteral("id"), layerName);
        QDomElement child = root.firstChildElement();
        while (!child.isNull()) {
            QDomElement next = child.nextSiblingElement();
            root.removeChild(child);
            group.appendChild(child);
            child = next;
        }
        root.appendChild(group);
        Q_UNUSED(firstChild);
        resultId = layerName;
    }

    stripGorn(root);
    return resultId;
}

QString PESvgImport::normalizeToString(const QString & svg, const QString & layerName)
{
    QDomDocument doc;
    if (!parse(svg, doc)) return svg;
    normalize(doc, layerName, nullptr);
    return doc.toString(4);
}
