# Parts Editor — sole-renderer rebuild (PCB-first)

## Why

One part was being drawn by **two independent renderers at once**:

1. `itemBase` — a QtSvg `QGraphicsSvgItem` (+ its `ConnectorItem` children) created by
   `SketchWidget::addItem()`. This is Fritzing's *part-view* machinery, inherited by the Parts Editor
   but not controllable from it.
2. `PEGraphicsItem` — the editor's per-element overlay, derived from the typed `PEDocument`.

Every symptom of the last effort was a seam between those two systems:

| Symptom | Seam |
|---|---|
| "old svg painted on top, our work under it" | renderer 1 paints over renderer 2 |
| 8 `+` connector marks stuck at scene (0,0) | renderer 1's `ConnectorItem`s never position-initialized in the PE |
| `+` marks far off the pads | the two renderers never shared a coordinate space |
| empty-id ghost rect at top-left | renderer 2 built an item for an unaddressable element |

Z-order / visibility / colourising patches kept the two renderers in temporary agreement; they always
drifted back. Per the Qt Graphics View contract ("the topmost item at a point receives the event"), the
correct model is **one owner of the render**, not two that must be reconciled every frame.

## Target architecture

The Parts Editor becomes the **sole renderer**. `itemBase` (the QtSvg backdrop) and its foreign
`ConnectorItem`s are **not added to the PE scene at all**. Each modelled SVG element becomes exactly one
top-level `PEGraphicsItem` that **paints its own body** (fill/stroke/layer colour) plus selection chrome.
There is no backdrop to be "on top of", and no foreign connector markers to be mispositioned.

This is the P0/P10 path already built, finally committed to:
- `PEDocument` / `PEElement` — typed SVG model (P0), round-trip stable.
- `PETransform::shapeFromElement` — decodes every kind (rect/line/circle/ellipse/polyline/polygon/
  path/text) to a `QPainterPath` (P10).
- `peviewlayermap` — maps part SVG layer id → `ViewLayer`, so self-paint can use Fritzing's layer colours.
- `PEDocument`-driven command layer (move/resize/create/remove) with undo — already id-keyed.

## Sequence (PCB only, one verified step at a time)

1. **Prove the overlay renders alone.** Stop adding `itemBase` to the PCB scene; let the `PEGraphicsItem`s
   paint their own bodies for the PCB view. If the part appears correct with no QtSvg backdrop, the
   architecture is proven. *(landed — see "Implementation status")*
2. **Colourise the self-paint** per layer (copper1 → `#fdde68`, copper0 → `#f9a435`, silkscreen → `#000`)
   using `SvgFileSplitter::fixColorRecurse` semantics, applied in `PEGraphicsItem::paint`.
3. **Layer toggle** (Top/Bottom copper) now becomes trivial: show/hide the copper1 vs copper0 `PEElement`s.
4. **Connector terminals**: draw `+` marks from the *same* `PEElement` as each pad → align by construction.
5. **Interactive edit** (select/drag/resize) — reuse the existing command layer; the backdrop is gone so
   nothing competes for clicks.
6. Extend the same sole-renderer path to icon / breadboard / schematic.

## Implementation status (as landed)

Shipped as a **standalone** sole-renderer surface (`tools/peditor` + `src/partseditor/{pesvgeditor,
pesvgpartloader,peprimitivegeom}`), deliberately *not* wired into the legacy `PEMainWindow`/`ItemBase`
pipeline yet — it proves and exercises steps 1–5 for the PCB view end-to-end:

- **Steps 1–2 (render + colourise):** `PESvgPartLoader::loadPart` reads a `.fzp`, resolves the
  `<views><pcbView><layers image>` path against the parts tree, and colourises the DOM in place using
  an exact-replica of `SvgFileSplitter::fixColorRecurse`/`setStrokeOrFill` (Qt Core/Xml only). The
  colouriser stops descent at nested layer-group ids (`isLayerGroupId`) so outer `copper0` never bleeds
  over inner `copper1` — golden-tested (`test_pesvgeditor::colourise_confinesNestedCopper`).
- **Step 3 (leaf decomposition):** `PESvgEditor` builds **one `QGraphicsSvgItem` per addressable leaf**
  graphic (not per layer group), positioned at `renderer->boundsOnElement(id).topLeft()`. Hit-testing
  picks the smallest-area containing item, so clicking a pad selects the pad, not its mounting hole or
  the layer it sits in. (Note: the leaf-per-item *position* fix — `setPos(boundsOnElement.topLeft)` —
  was the key correctness step; a bare `QGraphicsSvgItem` paints at item-local origin, so without it
  every leaf stacked at scene (0,0).)
- **Step 5 (interactive edit):** press selects (dashed outline parented to the item), drag previews in
  scene space, release maps the delta into the element's local user units via the item's scene
  transform and commits to the DOM by id, then reloads the renderer. Undo/redo replays move deltas.
  Verified offscreen (DOM `cx`/`cy` mutation + exact undo round-trip) and visually under Xvfb.

Deferred (honest gaps): `<path>` move (needs `PEPath` decoding — currently selected but not draggable,
`moveBy` is a deliberate no-op), rect `resizeTo` handles, create-new-primitive, save/export back to
`.svg`/`.fzp`, and folding this surface into the real `PEMainWindow` (steps 4 and 6).

## Invariants

- Exactly **one** scene item per modelled element; no QtSvg item in the PE scene.
- Edit commands key off element **id**; id-less elements get no item (they can't be addressed).
- Stored SVG == displayed geometry (transforms carried per item, not baked at load).
- Every step is verified in the running app before the next begins.

## Standalone proof-of-path (tools/peditor)

Rather than bolt the new renderer onto the live `PEMainWindow` (which kept feeding it the colourised /
reload-mutated `m_pcbDocument`), the sole-renderer was proven in a **fully separate Qt app** that shares
*nothing* with `pemainwindow.cpp`, `ItemBase`, `SketchWidget` or the PE reload path:

- **`src/partseditor/pesvgeditor.{h,cpp}` — `PESvgEditor`**: one `QSvgRenderer` over the part SVG plus one
  `QGraphicsSvgItem` per **addressable leaf** graphic (`setSharedRenderer` + `setElementId`), not per layer
  group. A bare `QGraphicsSvgItem` paints its element at the *item-local* origin, so each leaf item is
  explicitly `setPos(renderer->boundsOnElement(id).topLeft())`; without that every leaf would stack at scene
  (0,0). Positioning per leaf this way is what makes an individual pad hit-testable and movable while the
  nested `<g id='copper0'><g id='copper1'>…` layers still render in the right place. This is the render
  primitive the PE will adopt.
- **`src/partseditor/pesvgpartloader.{h,cpp}` — `PESvgPartLoader`**: opens a **part**, not a raw file. It
  reads a `.fzp`, resolves `<views><pcbView|breadboardView|schematicView><layers image="…">` against the
  parts `svg/<family>` tree, and — for the PCB view — colourises each recognised layer group with an
  inlined exact-replica of `SvgFileSplitter::fixColorRecurse` (+ `fixStyleAttribute`/`setStrokeOrFill`)
  and the same layer colours the app uses (`Copper1Color #fdde68`, `Copper0Color #f9a435`,
  `Silkscreen1Color #000`, `Silkscreen0Color #444`). No link into the app libraries (Qt Core/Xml only).
- **`tools/peditor/main.cpp`**: minimal `QMainWindow` — *File ▸ Open Part (.fzp)* and *Open Raw SVG*, with a
  *View ▸ PCB / Breadboard / Schematic* switch. It is the standalone harness for iterating on the render.

### Nested-copper confinement (why the colouriser deviates from the raw helper)
The app paints each layer group by its own `elementId` in its own renderer, so an outer layer never
repaints a nested inner one. A single-document colourise walks whole subtrees, so `fixColorRecurse` here
**does not descend into a child that is itself a recognised layer group** (`isLayerGroupId`). Verified in
both nesting orders:
- `sparkfun-connectors-rj45-8-pth-fix` (`copper0` ⊃ `copper1`): top copper `#fdde68` present, no copper0
  bleed over the top pads — golden-tested headless in `test_pesvgeditor::colourise_confinesNestedCopper`
  (inner `copper1` pad resolves to `#fdde68`, outer `copper0` to `#f9a435`).
- `20mm_8x8_dot_matrix_column_cathode_led_display` (whose `pcbView` resolves to the `copper1` ⊃ `copper0`
  `20x20mm_DIP_16_600mil_pcb.svg`): bottom copper `#f9a435` + silkscreen `#000` present and distinct
  (rendered PNG pixel-count check, earlier session).
Visual check (this session, Xvfb): the stock RJ45 PCB footprint (`sparkfun-connectors_rj45-8_pcb_fix.svg`)
renders as **8 copper pads** in the staggered 4+4 layout + **2 black mounting holes** (`nonconn0`/`nonconn1`),
each placed at its real document position — **no stale backdrop, no `+` marks at 0,0, no pads stacked at
the origin.**

### Next
Move / select / undo are wired and verified (see "Implementation status"). **Integration into the real
Parts Editor is planned in `docs/design/pe-integration-plan.md` — start there.** It lays out the
rewrite-not-retrofit approach: keep the clean shell (docks/tabs/metadata/connectors/fzp-save helpers),
replace only the broken `SketchWidget`/`ItemBase` PCB render+edit spine with `PESvgEditor`, and land it in
testable slices A–H (host → selection↔docks → DOM-writer save → create/connector → resize → path → other
views → retire legacy). Remaining editor gaps before the swap: `<path>` move, rect `resizeTo`,
create-new-primitive, save/export to `.svg`/`.fzp`.

