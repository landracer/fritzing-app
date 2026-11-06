#ifndef PEVIEWLAYERMAP_H
#define PEVIEWLAYERMAP_H

#include <QHash>
#include <QList>
#include <QString>

#include <QDomElement>

#include "../viewlayer.h"

/**
 * @brief Resolves "which <g> must a new graphic for this view go inside?" -- a lookup, not a guess.
 *
 * Why this exists. The parts editor used to create every primitive with a hardcoded empty parent id,
 * so PEMainWindow::addPrimitive() fell through to appending at the <svg> root. Real Fritzing part SVGs
 * are layered -- PCB is `copper1` wrapping `copper0`, plus `silkscreen`; schematic is a `schematic`
 * group; breadboard/icon are `module`/`icon`. A graphic that lives outside those groups is drawn on top
 * of the layer structure instead of within it, which is what produced the artifacts reported when
 * adding shapes to the PCB view. The existing code even conceded the problem: initSvgTree() warns that
 * "this version of the new Parts Editor can not deal with separate copper0 and copper1 layers ... so
 * editing may produce an invalid PCB view image".
 *
 * PEViewLayerMap inspects a loaded view's SVG once and records the layer groups that actually exist in
 * it, so a create can be routed to the correct parent and a create into an unknown layer can be
 * rejected loudly rather than silently corrupting the part.
 *
 * It is deliberately a plain value type (no QObject, no state of its own beyond the map) so it can be
 * unit-tested headless and reused by the document controller.
 */
class PEViewLayerMap {
public:
    /** Where a newly-created graphic for a view should be inserted. */
    struct Targets {
        QString defaultParentId;   ///< insert new non-copper graphics here (may be empty => svg root)
        QString copperParentId;    ///< insert conductor/copper graphics here (PCB only; else empty)
        bool    hasCopperPair = false;   ///< both copper0 and copper1 present, nested (the hard case)
        bool    known = false;           ///< false => we did not recognise this view's structure

        bool isValid() const { return known; }
    };

    PEViewLayerMap() = default;

    /**
     * @brief Inspect @p root (the <svg> element of one view's document) and record its layers.
     *
     * Only groups that are actually present are recorded, so a part that has no silkscreen group does
     * not get graphics routed to one. Detection is by the layer ids ViewLayer uses as its canonical
     * names (viewlayer.cpp), so this stays in step with the rest of the app.
     */
    void inspect(const QDomElement & root);

    /// The insertion targets previously recorded for @p viewID.
    Targets targetsFor(ViewLayer::ViewID viewID) const;

    /// True if inspect() has recorded a recognised structure for @p viewID.
    bool contains(ViewLayer::ViewID viewID) const;

    /// All layer group ids discovered for @p viewID, outermost first. Empty if unknown.
    QList<QString> layerIdsFor(ViewLayer::ViewID viewID) const;

    void clear() { m_targets.clear(); m_layerIds.clear(); }
    bool isEmpty() const { return m_targets.isEmpty(); }

    /**
     * @brief Pick the <g> a new graphic of kind @p isCopper should be appended to, under @p root.
     * @return the parent element, or a null element if the view's structure is unknown.
     *
     * Falls back to the document root only when the view genuinely has no recognised layer group, and
     * never when it has one that simply does not match -- that case returns null so the caller can
     * refuse instead of corrupting the file.
     */
    QDomElement resolveParent(const QDomElement & root,
                              ViewLayer::ViewID viewID,
                              bool isCopper = false) const;

private:
    QHash<int, Targets>     m_targets;
    QHash<int, QList<QString>> m_layerIds;
};

#endif // PEVIEWLAYERMAP_H
