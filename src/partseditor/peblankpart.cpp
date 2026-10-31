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

#include "peblankpart.h"

#include <QDate>
#include <QList>

#include "utils/textutils.h"

// Mirrors GraphicsUtils::SVGDPI / GraphicsUtils::StandardFritzingDPI. Inlined as literals (rather than
// including graphicsutils.h, which drags in QtWidgets/QGraphicsItem) to keep this model lightweight and
// testable in the core+xml-only suite -- same decoupling convention as PEConnector avoiding ViewLayer.
static constexpr double PeSvgDpi = 90.0;
static constexpr double PeStandardFritzingDpi = 1000.0;


// Fritzing's fixed view keys (view xmlName) and the matching graphic-group / <layer layerId> name.
// Kept here as literal strings (not ViewLayer::ViewID) to stay decoupled -- mirrors how PEConnector is
// keyed by the fzp <view> xml name. The caller passes these same strings; they must match ViewLayer's.
struct ViewKey {
    QString viewName;   // fzp <viewName> (viewIDXmlName: 'breadboardView' ...)
    QString layerName;  // <layer layerId> + graphic <g id> (viewLayerXmlName: 'breadboard' ...)
};

static const QList<ViewKey> & viewKeys()
{
    // Order mirrors the four editor tabs. layerName is the <g id> the editor draws into.
    static const QList<ViewKey> keys = {
        {QStringLiteral("breadboardView"), QStringLiteral("breadboard")},
        {QStringLiteral("schematicView"),  QStringLiteral("schematic")},
        {QStringLiteral("pcbView"),        QStringLiteral("pcb")},
        {QStringLiteral("iconView"),       QStringLiteral("icon")},
    };
    return keys;
}

QString PEBlankPart::newModuleID()
{
    return TextUtils::getRandText();
}

QDomElement PEBlankPart::buildModule(QDomDocument & doc, const QString & moduleID, const QString & title)
{
    QDomElement module = doc.createElement(QStringLiteral("module"));
    module.setAttribute(QStringLiteral("moduleId"), moduleID);

    QDomElement version = doc.createElement(QStringLiteral("version"));
    version.appendChild(doc.createTextNode(QStringLiteral("4")));
    module.appendChild(version);

    QDomElement date = doc.createElement(QStringLiteral("date"));
    date.appendChild(doc.createTextNode(QDate::currentDate().toString(Qt::ISODate)));
    module.appendChild(date);

    QDomElement label = doc.createElement(QStringLiteral("label"));
    module.appendChild(label);

    QDomElement author = doc.createElement(QStringLiteral("author"));
    module.appendChild(author);

    QDomElement titleEl = doc.createElement(QStringLiteral("title"));
    if (!title.isEmpty())
        titleEl.appendChild(doc.createTextNode(title));
    module.appendChild(titleEl);

    QDomElement tags = doc.createElement(QStringLiteral("tags"));
    module.appendChild(tags);

    QDomElement properties = doc.createElement(QStringLiteral("properties"));
    module.appendChild(properties);

    QDomElement connectors = doc.createElement(QStringLiteral("connectors"));
    module.appendChild(connectors);

    QDomElement views = doc.createElement(QStringLiteral("views"));
    Q_FOREACH (const ViewKey & key, viewKeys()) {
        QDomElement view = doc.createElement(key.viewName);
        QDomElement layers = doc.createElement(QStringLiteral("layers"));
        // Placeholder image path so the fzp is well-formed; the caller swaps in the real saved path.
        layers.setAttribute(QStringLiteral("image"), key.layerName + QStringLiteral("/") + moduleID + QStringLiteral(".svg"));
        QDomElement layer = doc.createElement(QStringLiteral("layer"));
        layer.setAttribute(QStringLiteral("layerId"), key.layerName);
        layers.appendChild(layer);
        view.appendChild(layers);
        views.appendChild(view);
    }
    module.appendChild(views);

    return module;
}

QString PEBlankPart::blankViewSvg(const QString & layerName, const QRectF & box)
{
    QString header = TextUtils::makeSVGHeader(PeSvgDpi, PeStandardFritzingDpi,
                                              box.width(), box.height());
    return header + QStringLiteral("<g id='%1'>\n</g>\n</svg>").arg(layerName);
}
