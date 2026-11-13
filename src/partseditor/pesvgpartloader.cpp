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

#include "pesvgpartloader.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

namespace {

// ---- ViewLayer layer colours (verbatim from src/viewlayer.cpp) -------------------------------
// Kept as literals so the standalone tool needs no link into the app libraries.
const char * kCopper1Color     = "#fdde68"; // Copper1Color (top copper)
const char * kCopper0Color     = "#f9a435"; // Copper0Color (bottom copper)
const char * kSilkscreen1Color = "#000000"; // Silkscreen1Color
const char * kSilkscreen0Color = "#444444"; // Silkscreen0Color

// ---- Exact-replica colouriser (SvgFileSplitter::fixStyleAttribute + setStrokeOrFill +
//      fixColorRecurse). Inlined so we match Fritzing's on-disk colour behaviour without
//      pulling textutils.cpp / svgfilesplitter.cpp (and their app dependencies) into this tool.

// TextUtils::fixStyleAttribute(element, style, attributeName): lift one "prop: value;" pair out of
// the style attribute into a real SVG attribute.
void fixOneStyleProp(QDomElement & element, QString & style, const QString & attributeName)
{
    static const QString findStyle("%1[\\s]*:[\\s]*([^;]*)[;]?");
    const QString str = findStyle.arg(attributeName);
    QRegularExpression sw(str);
    QRegularExpressionMatch match;
    if (style.indexOf(sw, 0, &match) >= 0) {
        const QString value = match.captured(1);
        style.remove(sw);
        element.setAttribute(attributeName, value);
    }
}

// TextUtils::fixStyleAttribute(element): normalise the style attribute into stroke/fill/... attrs.
void fixStyleAttribute(QDomElement & element)
{
    QString style = element.attribute("style");
    if (style.isEmpty()) return;

    static const char * props[] = {
        "stroke-width", "stroke", "fill", "fill-opacity",
        "stroke-opacity", "font-size", "stroke-dasharray"
    };
    for (const char * p : props) {
        fixOneStyleProp(element, style, QString::fromLatin1(p));
    }

    if (style.trimmed().isEmpty()) {
        element.removeAttribute("style");
    } else {
        element.setAttribute("style", style);
    }
}

// SvgFileSplitter::setStrokeOrFill(element, blackOnly=true, color, force): paint stroke (when present
// or forced) and fill (when present and not "none") with color, skipping "none".
void setStrokeOrFill(QDomElement & element, const QString & color, bool force)
{
    const QString stroke = element.attribute("stroke");
    if (!stroke.isEmpty() || force) {
        if (stroke.compare("none") != 0) {
            element.setAttribute("stroke", color);
        }
    }
    const QString fill = element.attribute("fill");
    if (!fill.isEmpty()) {
        if (fill.compare("none") != 0) {
            element.setAttribute("fill", color);
        }
    }
}

// SvgFileSplitter::fixColorRecurse: walk the subtree; on each element that is not an exception
// (black/#000000 ink we keep), repaint it. Byte-for-byte the app's algorithm.
//
// One deviation from the raw app helper is required for the nested-copper case: a PCB svg nests the
// copper layers (<g id='copper0'><g id='copper1'>…</g></g>). The app renders each layer group by its
// own elementId, so the outer layer never repaints the inner one. Our single-document colourise walks
// each group's subtree, so we must NOT recurse into a child that is itself a recognised layer group --
// otherwise the outer copper bleeds its colour over the inner layer. isLayerGroupId() gates descent so
// each layer's colour stays confined to its own group, matching the app's per-elementId paint.
bool isLayerGroupId(const QString & id)
{
    return id == "copper0" || id == "copper1" || id == "copper2" || id == "copper3"
        || id == "silkscreen" || id == "silkscreen0";
}

void fixColorRecurse(QDomElement & element, const QString & newColor, const QStringList & exceptions)
{
    fixStyleAttribute(element);

    bool gotException = false;
    const QString s = element.attribute("stroke");
    const QString f = element.attribute("fill");
    const QString id = element.attribute("id");
    for (const QString & e : exceptions) {
        Q_UNUSED(e);
        if (s.isEmpty()) {
            if (!f.isEmpty()) {
                if (exceptions.contains(f)) { gotException = true; break; }
            }
        } else {
            if (f.isEmpty()) {
                if (exceptions.contains(s)) { gotException = true; break; }
            } else {
                if (exceptions.contains(s) && exceptions.contains(f)) { gotException = true; break; }
            }
        }
    }
    if (!gotException) {
        setStrokeOrFill(element, newColor, !id.isEmpty());
    }

    QDomElement childElement = element.firstChildElement();
    while (!childElement.isNull()) {
        if (isLayerGroupId(childElement.attribute("id"))) {
            childElement = childElement.nextSiblingElement();
            continue;   // never bleed this layer's colour into a nested, different layer group
        }
        fixColorRecurse(childElement, newColor, exceptions);
        childElement = childElement.nextSiblingElement();
    }
}

// Paint a layer's own group root (always, even though its id looks like a layer id), then recurse
// into its non-layer children. fixColorRecurse itself stops at nested layer groups, which is correct
// for descendants but would skip the root we were explicitly asked to colourise.
void fixColorRecurseSelfThenChildren(QDomElement & layerGroup, const QString & newColor, const QStringList & exceptions)
{
    fixStyleAttribute(layerGroup);

    bool gotException = false;
    const QString s = layerGroup.attribute("stroke");
    const QString f = layerGroup.attribute("fill");
    for (const QString & e : exceptions) {
        Q_UNUSED(e);
        if (s.isEmpty()) { if (!f.isEmpty() && exceptions.contains(f)) { gotException = true; break; } }
        else { if (f.isEmpty()) { if (exceptions.contains(s)) { gotException = true; break; } }
               else if (exceptions.contains(s) && exceptions.contains(f)) { gotException = true; break; } }
    }
    if (!gotException) setStrokeOrFill(layerGroup, newColor, false);

    QDomElement childElement = layerGroup.firstChildElement();
    while (!childElement.isNull()) {
        if (isLayerGroupId(childElement.attribute("id"))) {
            childElement = childElement.nextSiblingElement();
            continue;
        }
        fixColorRecurse(childElement, newColor, exceptions);
        childElement = childElement.nextSiblingElement();
    }
}



// TextUtils::findElementWithAttribute: depth-first search for the first element with id==value.
QDomElement findElementWithId(const QDomElement & root, const QString & id)
{
    QDomElement e = root.firstChildElement();
    while (!e.isNull()) {
        if (e.attribute("id") == id) return e;
        QDomElement found = findElementWithId(e, id);
        if (!found.isNull()) return found;
        e = e.nextSiblingElement();
    }
    return QDomElement();
}

// Which layer groups this view colours, and with what colour. Mirrors ItemBase::setUpImage's PCB
// switch (breadboard/schematic views have no copper colourisation).
void colouriseLayerGroup(QDomDocument & doc, const char * layerId, const QString & color)
{
    QDomElement root = doc.documentElement();
    if (root.isNull()) return;
    QDomElement g = findElementWithId(root, QString::fromLatin1(layerId));
    if (g.isNull()) return;
    QStringList exceptions;
    exceptions << "black" << "#000000";
    // Colourise this layer's own group (g itself), but when descending, stop at any nested *different*
    // layer group so colours stay confined per-layer (matches the app's per-elementId paint). fixColorRecurse
    // paints the passed element, then recurses children with stopAtLayer=true.
    fixColorRecurseSelfThenChildren(g, color, exceptions);
}

} // namespace

QDomDocument PESvgPartLoader::colouriseForView(const QDomDocument & in, View view)
{
    QDomDocument doc = in; // deep copy; we mutate only the copy
    if (view != PcbView) {
        return doc; // breadboard / schematic are shown with their authored colours
    }
    colouriseLayerGroup(doc, "copper0",     QString::fromLatin1(kCopper0Color));
    colouriseLayerGroup(doc, "copper1",     QString::fromLatin1(kCopper1Color));
    colouriseLayerGroup(doc, "silkscreen0", QString::fromLatin1(kSilkscreen0Color));
    colouriseLayerGroup(doc, "silkscreen",  QString::fromLatin1(kSilkscreen1Color));
    return doc;
}

PESvgPartLoader::Result PESvgPartLoader::loadPart(const QString & fzpPath, View view, const QString & svgDirRoot)
{
    Result r;

    QFile fzp(fzpPath);
    if (!fzp.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Cannot open fzp: %1").arg(fzpPath);
        return r;
    }
    QDomDocument fzpDoc;
    if (!fzpDoc.setContent(&fzp)) {
        r.error = QStringLiteral("Cannot parse fzp: %1").arg(fzpPath);
        return r;
    }
    fzp.close();

    QDomElement module = fzpDoc.documentElement();
    if (module.isNull()) {
        r.error = QStringLiteral("Empty fzp: %1").arg(fzpPath);
        return r;
    }

    // Views live under <module><views>...; some older/legacy parts nest them directly under the
    // module, so look in <views> first then fall back to the module root.
    QDomElement viewsScope = module.firstChildElement("views");
    if (viewsScope.isNull()) viewsScope = module;

    const char * viewTag = (view == PcbView) ? "pcbView"
                         : (view == BreadboardView) ? "breadboardView"
                         : "schematicView";

    QDomElement viewElem = viewsScope.firstChildElement(QString::fromLatin1(viewTag));

    if (viewElem.isNull()) {
        r.error = QStringLiteral("Part has no %1: %2").arg(QString::fromLatin1(viewTag), fzpPath);
        return r;
    }
    QDomElement layers = viewElem.firstChildElement("layers");
    if (layers.isNull()) {
        r.error = QStringLiteral("%1 has no <layers>: %2").arg(QString::fromLatin1(viewTag), fzpPath);
        return r;
    }

    const QString imageRel = layers.attribute("image");
    if (imageRel.isEmpty()) {
        r.error = QStringLiteral("<layers> has no image attribute: %1").arg(fzpPath);
        return r;
    }

    // Record the layers this view declares so the editor can filter to them.
    for (QDomElement le = layers.firstChildElement("layer"); !le.isNull(); le = le.nextSiblingElement("layer")) {
        const QString lid = le.attribute("layerId");
        if (!lid.isEmpty()) r.visibleLayers << lid;
    }

    QDomElement titleElem = module.firstChildElement("title");
    if (!titleElem.isNull()) r.title = titleElem.text().trimmed();

    // Resolve image path against the parts' svg/<family> root.
    const QString svgPath = QDir(svgDirRoot).absoluteFilePath(imageRel);
    r.svgPath  = svgPath;
    r.svgLabel = QFileInfo(svgPath).fileName();

    QFile svg(svgPath);
    if (!svg.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Cannot open svg referenced by fzp: %1").arg(svgPath);
        return r;
    }
    QDomDocument svgDoc;
    if (!svgDoc.setContent(&svg)) {
        r.error = QStringLiteral("Cannot parse svg: %1").arg(svgPath);
        return r;
    }
    svg.close();

    // RAW-SOURCE-OF-TRUTH: hand the editor the svg EXACTLY as authored on disk -- no recolouring. Library
    // svgs already author each layer's colour per layer-group (e.g. copper stroke #f7bd13), so the raw DOM
    // already displays correctly, and PEItemPlan's per-view layer filter selects what is shown. This is what
    // lets Save write the bytes straight back without leaking any view colour into the file.
    r.document = svgDoc;
    r.ok = true;
    return r;
}

