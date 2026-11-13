# Parts Editor — self-paint renderer (the real sole-renderer landing)

> **Read me.** How the PCB double-draw was actually fixed. A–E are **landed** (A render, B rewiring, C
> hit-test/move/undo, D selection chrome + path node-edit, E dead-code sweep deleting the legacy cluster). No new
> libs, no greenfield — QtSvg-item isolation is impossible, so the editor self-paints.

## 1. Root cause (proven, not theorised)

`PESvgEditor` rendered each graphic as its own `QGraphicsSvgItem` + `setElementId(id)`. Qt's
`QSvgRenderer::render(painter, elementId)` does **not** isolate that element — it repaints the **whole
document** clipped to the element's bbox (verified: rendering element `A` also painted sibling `B`). So a
whole-group item + a leaf inside that group always double-paint. `PEItemPlan`'s itemization is *correct as a
decision* (descend-or-whole, no id twice) but it fed a renderer that cannot honor "draw only this subtree."
The rest of Fritzing avoids this by **self-painting**: `PETransform::shapeFromElement()` decodes an element to
a `QPainterPath`; fill/stroke only that path. No clip bug, and no `<path>`/`<text>` ceiling.

## 2. What was built (Slice A + B — landed)

`src/partseditor/peselfpaintitem.{h,cpp}` — a `QGraphicsObject` that:
- decodes the primitive(s) it **owns** (one editable leaf, or every descendant of an opaque group) into
  `QPainterPath`s via `PETransform::shapeFromElement`;
- `paint()` fills/strokes **only** those, each with its own fill/stroke/stroke-width/fill-rule;
- `boundingRect()` = union bbox (+ half the widest stroke) — exact path hit-testing;
- `setElement(element)` binds the unit; **item transform is always identity** (see §3).

`PESvgEditor::rebuildScene()` now: `PEItemPlan::plan(root, visibleLayers)` → per planned unit `findById` +
`PETransform::flatten(el)` → ONE `PESelfPaintItem` (editable leaf carries `data(0)=id` + selectable; opaque
group paints whole subtree, not selectable). Deleted: `QSvgRenderer`, `QGraphicsSvgItem`, `setElementId`,
`applyDocToRenderer`, `m_renderer`, `m_itemBasePos`. Move = `setPos(sceneDelta)` preview + `moveBy` commit +
rebuild; undo/redo replay the id-keyed delta and rebuild.

## 2b. Hit-test — smallest-area, on PESelfPaintItem (Slice C)

`PESelfPaintItem::hitTestId(scene, point)` (public static) is the selection resolver `PESvgEditor::handlePress`
calls: among every item carrying an id under `data(0)` whose `boundingRect()` contains the point (mapped via
`mapFromScene` — identity transform, so exact), it returns the id of the **smallest-area** one. A pad drawn over
a bigger mounting-hole box / layer background therefore selects the *pad*, not the layer. Opaque groups and the
selection outline carry no `data(0)` → never selected. It lives on the item (not `PESvgEditor`) so it is
golden-tested headlessly against a scene of `PESelfPaintItem`s, and reused verbatim by the editor.

## 3. Transform model — flatten-at-load (the chosen contract)

Chosen over carrying per-item ancestor transforms (which I twice double-applied while prototyping). Every
planned element is flattened (`PETransform::flatten`, idempotent) before binding: its inherited ancestor+own
`transform` is baked into absolute geometry attrs and the `transform` attribute is dropped. So items are at
identity, decoded paths are already absolute == scene coords, and a scene-space drag delta maps straight onto
the raw attrs. Colourise stays at load, so self-paint reads already-coloured attrs. `flatten` is idempotent
(already-absolute attrs → no-op on re-flatten), which is what makes a move→commit→rebuild→undo cycle lossless.

### 3b. PEItemPlan: top-level editable leaves (Slice C root-cause fix)

Slice C's flat-pad interaction test (`<svg><circle id='pad'/></svg>` — a primitive that is a DIRECT child of
`<svg>`) exposed a real `PEItemPlan::plan` defect: the old top loop descended INTO groups but never emitted a
top-level leaf, so any primitive directly under `<svg>` (a flat part) was silently un-selectable. Fix: the
top level now applies the same rule set to each root child (emit editable leaf / emit whole opaque group /
descend into a group-with-editables), keeping the `visibleLayers` filter for top-level layer groups. `walk()`
handles nested levels identically.

## 4. Selection chrome — marquee + grips + path node edit (Slice D, landed)

Selecting a graphic draws a dashed, **cosmetic** (zoom-independent) marquee of the element's absolute bounds,
plus **handles**:
- A **`<rect>`** (the only kind that stores a box) gets the 8 grips (4 corners + 4 edge midpoints). Grabbing one
  and dragging sets the grabbed edge(s) by the drag delta; on release `PEPrimGeom::resizeRect(el, toBox)` commits.
- A **`<path>`** has its decoded segment **anchors** rendered as draggable node grips; grabbing node `i` and
  dragging calls `PEPrimGeom::editPathAnchor(el, i, scenePoint)` (rewrites that segment's anchor, preserving the
  source's abs/rel convention). So a `<path>` is edited by its **nodes**, not a bbox — its geometry attrs are a
  `d`, not a box.
- Other primitives (circle/line/…) store a position, not a size: they have **no** resize affordance; they move by
  body-drag (`moveBy`).

All chrome is drawn in absolute scene coords (elements are flattened to absolute), zoom-independent by dividing
the on-screen grip size by `m_view->transform().m11()`. Grip hit-test = nearest center within half a grip; node
hit-test = nearest anchor within tolerance. `PESvgEditor::handlePress` gives a grabbed grip/node of the *current*
selection priority over re-hitting the body. Undo is a tagged `UndoEntry` (Move: delta; Resize: from→to box;
Anchor: segmentIndex + from→to anchor), so each kind replays/reverses exactly.

The enabling root-cause fix: `PEItemPlan::isEditableGraphic` previously required `localBounds(el).isValid()`, but
`localBounds` is deferred (empty) for `<path>` — so a path was **never selectable/movable**. Fixed: a `<path>` is
editable iff its anchors decode (`!pathAnchors(el).isEmpty()`), so paths are now first-class selectable/editable.
The 4 pure `geom_*` path-anchor tests + 2 controller `chrome_*` tests (rect grip round-trip, path node-edit
round-trip, both undo-tested) prove it.

## 5. Status after Slice E (dead-code sweep — done)

The legacy QtSvg-isolation cluster (`PEGraphicsItem`/`PESvgView`/`PEToolView`/`PEDocument`/`PEElement`/`PEPrimitive`/
`PEConnector`/`PEBlankPart`/`PESvgImport`) is deleted; `pri/partseditor.pri` carries only live units. Remaining
non-code gap: true mouse-click QA on the GUI still needs a human (the logic behind it is golden-tested headlessly).

## 6. Risks / deferred (honest)
- `<text>` self-paint: `shapeFromElement` returns the anchor point only (zero-area) → text non-visual until a
  text decode exists (deferred, as in `peprimitivegeom`/`peitemplan`).
- Paint fidelity (linecap/dash/opacity): current paint is fill/stroke/width only (what parts use); extend later.
- A `<path>` body-drag (translate the whole path) is still a no-op (`moveBy` Path → false); paths are moved by
  dragging their **node grips** one at a time, which is the intended node-editing model for this surface.

## 7. Definition of done
PCB/schematic render with **no double**; pads individually selectable & movable; undo works; `PESvgEditor` no
longer uses QtSvg item isolation (done). The whole-group-repaints-leaf bug class cannot recur (done by §3).
Selection chrome (marquee + 8 grips for rect + path node grips) with move/resize/node-edit + undo is landed (§4).
**Slice E (done):** the entire legacy QtSvg-isolation cluster is deleted — `PEGraphicsItem`/`PESvgView`/`PEToolView`/
`PEDocument`/`PEElement`/`PEPrimitive`/`PEConnector`/`PEBlankPart`/`PESvgImport` and their `test_pedocument` suite
(live `PEPath` goldens ported into `test_pesvgeditor`). So DO NOT reintroduce `ItemBase`/`SketchWidget`/`ModelPart`
or the old `PEElement`/`PEGraphicsItem` double-render — the self-paint item decodes via `PETransform::shapeFromElement`
alone; `PEPath` (live) supplies path decode/round-trip.
