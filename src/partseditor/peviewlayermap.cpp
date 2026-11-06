#include "peviewlayermap.h"

#include <QDomDocument>

namespace {

// Canonical layer group ids, taken from ViewLayer's own name table (src/viewlayer.cpp) so this cannot
// silently drift from the rest of the application.
const char * const kIconLayer       = "icon";
const char * const kBreadboardLayer = "module";
const char * const kSchematicLayer  = "schematic";
const char * const kPcbSilkscreen   = "silkscreen";
const char * const kPcbCopper0      = "copper0";
const char * const kPcbCopper1      = "copper1";

/**
 * Depth-first search for a <g> whose id attribute equals @p id. Part SVGs nest their layer groups
 * (notably copper0 inside copper1), so a first-level scan is not enough.
 */
QDomElement findGroup(const QDomElement & scope, const QString & id) {
    if (scope.isNull() || id.isEmpty()) return QDomElement();
    for (QDomElement g = scope.firstChildElement(QStringLiteral("g")); !g.isNull();
         g = g.nextSiblingElement(QStringLiteral("g"))) {
        if (g.attribute(QStringLiteral("id")) == id) return g;
        const QDomElement deep = findGroup(g, id);
        if (!deep.isNull()) return deep;
    }
    return QDomElement();
}

QString idOf(const QDomElement & e) { return e.attribute(QStringLiteral("id")); }

}   // namespace

void PEViewLayerMap::inspect(const QDomElement & root) {
    if (root.isNull()) return;

    const QString rootId = idOf(root);

    const QDomElement gModule    = findGroup(root, QString::fromLatin1(kBreadboardLayer));
    const QDomElement gSchematic = findGroup(root, QString::fromLatin1(kSchematicLayer));
    const QDomElement gIcon      = findGroup(root, QString::fromLatin1(kIconLayer));
    const QDomElement gSilk      = findGroup(root, QString::fromLatin1(kPcbSilkscreen));
    const QDomElement gCopper0   = findGroup(root, QString::fromLatin1(kPcbCopper0));
    const QDomElement gCopper1   = findGroup(root, QString::fromLatin1(kPcbCopper1));

    // PCB: identified by the presence of a copper layer. New graphics default to silkscreen, the
    // non-conductive layer that outlines and markings belong on; explicit copper requests go to the
    // OUTERMOST copper group so a nested copper0 keeps its position in the tree.
    if (!gCopper0.isNull() || !gCopper1.isNull()) {
        Targets t;
        t.known = true;
        t.defaultParentId = !gSilk.isNull()    ? idOf(gSilk)
                          : !gCopper1.isNull() ? idOf(gCopper1)
                                               : idOf(gCopper0);
        t.copperParentId  = !gCopper1.isNull() ? idOf(gCopper1) : idOf(gCopper0);
        // The historically-mishandled shape: both copper layers present and nested. Placement is still
        // correct here; the flag lets callers surface that the part is in that form.
        t.hasCopperPair = !gCopper0.isNull() && !gCopper1.isNull();
        m_targets.insert(ViewLayer::PCBView, t);

        QList<QString> ids;
        if (!gCopper1.isNull()) ids << idOf(gCopper1);
        if (!gCopper0.isNull()) ids << idOf(gCopper0);
        if (!gSilk.isNull())    ids << idOf(gSilk);
        m_layerIds.insert(ViewLayer::PCBView, ids);
        return;
    }

    // Schematic / breadboard / icon: one enclosing group each.
    struct Probe { ViewLayer::ViewID viewID; QDomElement g; };
    const Probe probes[] = {
        { ViewLayer::SchematicView,  gSchematic },
        { ViewLayer::BreadboardView, gModule },
        { ViewLayer::IconView,       gIcon },
    };
    for (const Probe & p : probes) {
        if (p.g.isNull()) continue;
        Targets t;
        t.known = true;
        t.defaultParentId = idOf(p.g);
        m_targets.insert(p.viewID, t);
        m_layerIds.insert(p.viewID, QStringList() << t.defaultParentId);
    }

    // Some parts name the <svg> root itself after the view layer rather than wrapping a <g>.
    if (rootId.compare(QString::fromLatin1(kSchematicLayer)) == 0 && !m_targets.contains(ViewLayer::SchematicView)) {
        Targets t;
        t.known = true;
        t.defaultParentId = rootId;
        m_targets.insert(ViewLayer::SchematicView, t);
        m_layerIds.insert(ViewLayer::SchematicView, QStringList() << rootId);
    }
}

PEViewLayerMap::Targets PEViewLayerMap::targetsFor(ViewLayer::ViewID viewID) const {
    return m_targets.value(static_cast<int>(viewID));
}

bool PEViewLayerMap::contains(ViewLayer::ViewID viewID) const {
    return m_targets.contains(static_cast<int>(viewID));
}

QList<QString> PEViewLayerMap::layerIdsFor(ViewLayer::ViewID viewID) const {
    return m_layerIds.value(static_cast<int>(viewID));
}

QDomElement PEViewLayerMap::resolveParent(const QDomElement & root,
                                          ViewLayer::ViewID viewID,
                                          bool isCopper) const {
    if (root.isNull()) return QDomElement();

    const Targets t = targetsFor(viewID);

    // Unknown structure: refuse rather than append to the root. This is the deliberate behaviour
    // change -- the old code appended to the root unconditionally, which is what corrupted layered
    // parts. A caller that genuinely wants a root append can do it explicitly.
    if (!t.known) return QDomElement();

    const QString wanted = (isCopper && !t.copperParentId.isEmpty()) ? t.copperParentId
                                                                    : t.defaultParentId;
    if (wanted.isEmpty()) return QDomElement();

    if (idOf(root) == wanted) return root;   // the target may BE the root

    return findGroup(root, wanted);   // null if it disappeared since inspect(); caller must handle
}
