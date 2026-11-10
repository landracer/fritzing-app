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

#ifndef PESVGPARTLOADER_H
#define PESVGPARTLOADER_H

#include <QString>
#include <QStringList>
#include <QDomDocument>

// Loads a part's view SVG directly from a .fzp + the parts tree, the same mapping Fritzing's
// ReferenceModel uses to resolve <pcbView>/<breadboardView>/<schematicView><layers image="...">,
// WITHOUT the ReferenceModel/PartBase/ModelPart machinery. It exists so the standalone editor
// (tools/peditor) can open a part from the library and show a *correctly colourised* view --
// exactly what the raw "Open SVG" flow could not do.
//
// The colourisation mirrors the app's own path: FSvgRenderer::loadAux / ItemBase::setUpImage
// colourise each recognised layer group with SvgFileSplitter::fixColorRecurse(group, <layer colour>,
// {black, #000000}). We inline an equivalent (setStrokeOrFill + fixStyleAttribute) so this class
// depends only on Qt Xml/Core -- no link into the Fritzing app libraries. See
// docs/design/pe-sole-renderer.md.
class PESvgPartLoader
{
public:
    enum View {
        PcbView,
        BreadboardView,
        SchematicView
    };

    struct Result {
        bool        ok = false;
        QDomDocument document;     // the resolved + colourised svg, ready for PESvgEditor::setDocument
        QString     svgPath;       // absolute path of the svg file that was loaded
        QString     svgLabel;      // display label (file name of the svg)
        QString     title;         // part <title> from the fzp (display only)
        QStringList visibleLayers; // layerIds the fzp declares for this view ("" = show all)
        QString     error;
    };

    // `svgDirRoot` is the directory the fzp image paths resolve against, i.e. the parts' svg/<family>
    // dir (e.g. <repo>/fritzing-parts/svg/core). A fzp image attribute looks like
    // "pcb/sparkfun-connectors_rj45-8_pcb_fix.svg" and resolves to <svgDirRoot>/pcb/<...>.svg.
    static Result loadPart(const QString & fzpPath, View view, const QString & svgDirRoot);

    // Colourise a raw part svg the same way the app does. Public so callers that already hold a
    // document (e.g. a raw-svg open) can opt into the same colour rules.
    static QDomDocument colouriseForView(const QDomDocument & in, View view);
};

#endif // PESVGPARTLOADER_H
