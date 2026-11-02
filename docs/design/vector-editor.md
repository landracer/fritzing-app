# Parts Editor → de-facto parts/symbol design editor (A-to-Z)

> Design reference. Newest decisions at top.
>
> **Re-scoped (Session 13.1).** The goal is *not* to port svgedit/Inkscape. Inkscape (C++) and
> svgedit are **reference implementations only**. We are turning the live parts editor into a
> de-facto-standard editor purpose-built for **schematic + parts design**: the user's whole existing
> part library is at hand as a base, graphics are fully editable in-app, and SVG round-trips in/out.
> The graphics-editing layer is *one* feature of a coherent parts-creation tool, not the whole spec.

## A. The A-to-Z flow the editor must serve

1. **Start from existing.** Open any existing part (symbol/footprint) from the library as the base.
   — *already half-built:* the live PE inherits the bin/palette system (`pemainwindow.cpp:500` spins
   up `BinManager` with `resources/bins/pe.fzb`; `setInitialItem` at `:698` already loads an existing
   `ModelPart` as the base). Enhancement: browse/search the full library *inside* the editor, not just
   the default `pe.fzb`.
2. **Edit the graphics.** Real in-app vector editing (select, transform, node editing, primitives,
   text, layers) on the part's SVG. — *this is the core missing capability* (today it is metadata +
   static preview + one terminal-point handle; see §C).
3. **Import / export SVG.** Easy SVG in and out. Export already exists (`exportSvg`,
   `exportNormalizedSVG`, Eagle out via `fritzing2eagle`); **import of arbitrary SVG + make-the-editor
   read/normalize it** is the open half.
4. **Save as a part / create from blank.** Persist to `.fzp`+`.svg` (already works); also support a
   **blank** new part when nothing in the library matches.

## B. The graphics-editing capability (the core new layer)

The single biggest gap: real in-app vector editing of the part's SVG. This is *one* (central) feature
of the editor, framed as the editing surface behind the A-to-Z flow — not a port of an engine. We
build a Fritzing-shaped editing layer that operates on the part's `QDomDocument`, with Inkscape's
node/selection behavior and svgedit's `core/` (pathData ⇄ segment list, grips, command-undo, sanitize)
as behavioral references.

### B.1 Architecture decision (Session 24) — **fully-live flattened**, one mental model

The overlay model (see §C) is *not* an editor: the part is a baked `QGraphicsSvgItem` and each graphic
gets an invisible hit-`QGraphicsRectItem` on top. A "graphic" and its "handle" are decoupled by
construction, which is exactly why no amount of command wiring makes it feel editable.

**Decision (user, Session 24): go fully-live/flattened.** Every *modeled primitive* (rect, circle,
ellipse, line, polyline, polygon, path, text) becomes a **live editable `QGraphicsItem` that renders
itself** — the item *is* the graphic, selectable and draggable directly, not a hit-rect over a picture.
A `<g>` group is **not** a live item; it contributes a **transform context only**. This is the lower-risk
path precisely because it is what already renders correctly today: `PEDocument::build` flattens to a
flat `QList<PEElement>` (hierarchy is not preserved) and `getPixelBounds` already bakes each element's
inherited group transform into its absolute bounds (`matrix.mapRect(r)`). The typed model and the
renderer already agree on *flatten + bake-transform*; making the item live is the last step to close the
loop, not a new idea.

Consequences we accept, explicitly:
- **Flatten + baked transform on load.** When a graphic lived under `<g transform=…>`, its local geometry
  attrs are rewritten with the transform folded to absolute user units at load, so "what you see is what
  is stored." The group's `<g id=layerName>` stays as the *container* (it is what `insertElement` appends
  into and what `removeGorn`/`svgNSOnly` preserve), but it is not itself an editable handle.
- **`PEElement` must carry the transform context it was baked from** (see §B.2) so re-render reproduces the
  original appearance and a "move" carries the element's *local-space* delta, not a screen delta.

### B.2 `PEElement` transform-context gap (the actual crux — resolve before live items land)

Today `PEDocument::build` stores `id`, `cssClass`, and (for rect) attribute-derived bounds — but **not**
the element's `transform` or the inherited transform chain. `getPixelBounds` bakes the transform for
*display* only; on the write-back path `moveBy`/`editPathAnchor` rewrite the element's own attrs in its
**own** coordinate space, and the group transform is silently re-applied on re-render. For the
terminal-point drag that is invisible; for a general move/resize it is a latent correctness bug the live
model must own.

**Decision — open (first thing to pin):** when the user moves a graphic that lived under a transformed
group, do we (a) rewrite the element's local attrs by the *inverse-transformed* delta so the group
transform re-applies and it lands where dragged, or (b) **flatten the transform away at load** (store the
already-baked absolute geometry on `PEElement` and drop the group transform for that subtree, so stored
== displayed with no re-apply)? Recommendation: **(b) flatten-and-drop at load** — one coordinate space,
no re-apply, matches "what you see is what's stored," and it is what `getPixelBounds` already computes.
It costs us the ability to move a whole group as a unit (future: re-introduce group selection as a set of
selected flat items, not a nested live item).

## C. Ground truth (what exists today)

- **Was never a vector editor (until P0–P10).** Historically `initSvgTree()` (`pemainwindow.cpp:1423`)
  parsed the part's SVG into a `QDomDocument`, normalized it via `FSvgRenderer::loadSvg(...)`, then did a
  depth-first walk and created **one invisible hit-`PEGraphicsItem` (a `QGraphicsRectItem`) per element** over
  a single baked `QGraphicsSvgItem`. `PEGraphicsItem` was a pure hit-target rect with only hover/press/move
  and a single **terminal-point** handle — no primitive creation, no node/path editing.
- **Now (post-P9/P10): live self-rendering items.** `initSvgTree` builds the typed view (`PEDocument::build`)
  and calls `flattenInPlace()` (P9) to bake each leaf's group transform into absolute attrs, then creates one
  **self-rendering `PEGraphicsItem`** per element via `makePegi(...)` → `scene()->addItem(...)`, positioned at
  `itemBase->pos() + getPixelBounds(element)` (now reads the element's own absolute attrs — no renderer
  bounds, no px scaling). `PEGraphicsItem::paint` renders the element's own geometry (fill+stroke) via
  `PETransform::shapeFromElement` (P10); primitive creation (`createPrimitiveRequested` → `PEPrimitive::make`)
  and path node-editing grips are wired (P2/P3). This is the §B.1 fully-live model replacing the old overlay.
- The *old* editor (`partseditormainwindow`, `partseditorview`, old `svgview`) is parked in
  `src/partseditor/obsolete/` and **commented out** in `pri/partseditor.pri`.
- Round-trip: on save the DOM is serialized back with `removeGorn(...)`/`writeXml(...)`
  (`pemainwindow.cpp:937,1729,1870,...`); the `QDomDocument` is the source of truth.

Consequence: today, to author *any* new part graphic you must leave the app (real Inkscape) or
bake a symbol with the parametric `fritzing-schematic` plugin. That is the gap this work closes.

**Ground-truth (current, post-P10).** `initSvgTree` builds, per element, a **self-rendering** `PEGraphicsItem`
via `makePegi(...)` → `itemBase->scene()->addItem(...)` (`pemainwindow.cpp:3124`), each positioned at
`itemBase->pos() + getPixelBounds(element)` — the element's own **absolute (flattened)** geometry. `paint`
renders that geometry itself (`PETransform::shapeFromElement` + fill/stroke), so the graphic you see *is* the
live item, not a baked whole-view raster under an invisible rect. The pre-P10 overlay model (one baked
`QGraphicsSvgItem` of the whole view + per-element invisible hit-rects) is what P8–P10 replaced.

## D. Editing model (reference behavior → Qt)

Behavioral mapping (reference *behavior*, re-implemented in Qt — not line-by-line JS):

| capability | Inkscape / svgedit reference | Qt PE target |
|---|---|---|
| node/path editing (anchors + bezier handles) | Inkscape node-select; svgedit `path.js` + `path-method.js` segment list | `PEPathItem` segment list (QPainterPath) + `PENodeItem` grips |
| in-place attribute edit of selected element | svgedit `selected-elem.js` | `PEItem::setAttribute` + command |
| primitive creation | svgedit `draw.js` modes | `PEToolView` mode router → create element |
| transforms / coordinate math | `2geom`; svgedit `math.js` transformPoint/getMatrix | QGraphicsItem mapToScene/mapFromScene |
| layers | svgedit `layer.js` | `PEDocument` `<g>` layer groups |
| undo | svgedit `history.js` ChangeElementCommand | existing `pecommands` (QUndoCommand) |
| units | svgedit `units.js` | `PEUtils::convertUnitsStr` (already in `pesvgview.cpp`) |
| sanitize | svgedit `sanitize.js` | keep hand-edited/imported SVG Fritzing-valid |
| connector semantics | svgedit `ext-connector` side/marker | id-convention preserved as node-edit special case |

## E. Invariants we keep

1. **`QDomDocument` stays the source of truth.** The new model is a *typed view* over the existing
   DOM (`m_breadboardDocument`/`m_schematicDocument`/`m_pcbDocument`/`m_iconDocument`). Edits mutate
   the same DOM the rest of the app consumes.
2. **Connector semantics are a layer on top of generic primitives.** A "connector" is still an SVG
   node `connectorXpin` (+ optional `connectorXterminal`); the current terminal-point handle
   (`pegraphicsitem.cpp`, wired at `pemainwindow.cpp:1499`) becomes one case of node editing.
3. **Every edit is a command** (extend `pecommands.*`); no silent DOM mutation outside a command.
4. **Golden-tested** logic (AGENTS.md §4): `svg → model → svg` byte-stable + geometry goldens,
   verified by a fresh clean aggregate `-j` build (Session 12 lesson).

## F. Phased slices (each: feature branch → PR into develop → green)

- **P0 — typed DOM model + round-trip contract (inert). ✅ LANDED (Session 14).** `PEDocument`/`PEElement`
  (in `src/partseditor/pedocument.{h,cpp}`) as a typed view over the DOM (`build(doc)` walks depth-first,
  document-order; root `<svg>` + each `<g>` are modelled nodes, matching live `initSvgTree`); `toXml()` is
  pure `QDomDocument::toString()` so `build`/`toXml` is byte-stable. Golden `svg→model→svg` round-trip +
  kind/id/class/connectorIndex extraction + unknown-not-dropped, in `tests/auto/test_pedocument` (Boost,
  all-explicit `.pro`), wired into `pri/partseditor.pri` + `auto.pro`. No UI change. Foundation.
- **P1 — real item model. ✅ LANDED (Session 15, foundation only).** The live `initSvgTree` now
  drives overlay-item creation off `PEDocument` (behavior-identical refactor). The *editable*
  `PEPathItem`/`PERectItem`/… `QGraphics*Item`s that *replace* the rect overlay are P2's job; P1
  makes the typed model load-bearing (order + kind gate) without changing what's on screen.
- **P2 — node editing. ✅ LANDED (Sessions 16–17, model op).** `PEPath`/`PEPathSegment`
  (`src/partseditor/pepath.{h,cpp}`): decode an SVG `d` → an ordered typed segment list (command
  char + relative + args), reusing the existing `SVGPathLexer`/`SVGPathParser`; `serialize()` →
  back to `d`; `anchors()`/`points()` resolve each segment's anchor/handle in absolute user units
  against the running cursor. Round-trip (`fromD` → `serialize` → `fromD`) is stable.
  `PEElement::editPathAnchor(segmentIndex, anchor)` (Session 17) is the **node-edit operation**:
  it decodes the element's `d`, replaces that segment's anchor (encoding the point back in the
  segment's own absolute/relative convention), and returns the new `d`.
- **P2 live grip. ✅ LANDED (Session 19).** `PEElement::pathAnchors()` returns the resolved segment
  end-points; `PEGraphicsItem` (for a `path` element) paints one grip per anchor (mapped into item
  pixel coords) and on mouse-press hit-tests the nearest grip, on drag maps pixel→document units and
  emits `pathAnchorMoved`/`pathAnchorChanged`. `PEMainWindow::pathAnchorChangedAux` wraps the live
  writeback in `EditPathAnchorCommand` (→ `PEMainWindow::editPathAnchor` → DOM `d` + re-render +
  `updateChangeCount`), mirroring the `MoveTerminalPointCommand`/`moveTerminalPoint` pattern.
- **P3 create-tool trigger. ✅ LANDED (Session 20).** `PEToolView` now has a graphic-tools row (rect/
  circle/ellipse/line/path/polyline/polygon buttons + an "add connector from selected graphic" button)
  emitting `createPrimitiveRequested(kind)`/`addConnectorRequested()`; `PEMainWindow::createPrimitiveRequested`
  builds the element via `PEPrimitive::make` (id = moduleID.counter) and pushes `AddPrimitiveCommand`.

- **P4 — connector semantics. ✅ LANDED (model + live trigger, Session 20).** `PEConnector`
  (`src/partseditor/peconnector.{h,cpp}`) authors/edits the fzp `<connector><views><view><p svgId
  terminalId/></view></views></connector>` block purely (`authorConnector`/`rebindGraphic`/
  `graphicSvgId`/`terminalId`, keyed by the fzp `<view>` xml name). The live `addConnectorRequested`
  slot binds a new connector to the target graphic's svg id and refreshes the connector list; the
  terminal handle rides on that graphic via the P2 node-edit grips.

- **P5 — library-at-hand. ✅ LANDED (Session 21).** The parts editor had *no* part browser at all —
  its `initDock()` had the `HtmlInfoView` + `BinManager` construction commented out, so "start from an
  existing part" was only reachable from the *main* app's palette. `PEMainWindow::initDock()` now
  builds the full dock and calls `initStandardBins()` (every discoverable bin — core + contrib + user —
  *and* the Search bin) instead of the old single `:/resources/bins/pe.fzb`, so the whole library is
  browsable and searchable inside the editor. Picking "Edit Part (new parts editor)…" in that dock
  routes through the inherited `MainWindow::getPartsEditorNewAnd` → `openNewPartsEditor`, which
  `qobject_cast`s the top-level to this `PEMainWindow` and calls `setInitialItem()` on it — no new
  routing needed; the base part swaps in place.
- **P6 — SVG import + create-from-blank. ✅ LANDED (Session 22).** Two halves, both pure + golden-tested
  plus live File-menu wiring:
  - **create-from-blank** — `PEBlankPart` (`src/partseditor/peblankpart.{h,cpp}`): `newModuleID()` (a fresh
    id), `buildModule(doc, moduleID, title)` builds a minimal valid fzp `<module>` (version/date/label/
    author/title/tags/properties + the four `<view>`s' `<layers image><layer layerId/>` + empty `<connectors/>`),
    and `blankViewSvg(layerName, box)` writes a blank per-view `<svg>` with one empty `<g id=layerName>`.
  - **svg-import** — `PESvgImport` (`src/partseditor/pesvgimport.{h,cpp}`): `parse`/`normalize`/
    `normalizeToString` take a foreign svg and normalize it to a single canonical `<g id=layerName>` group
    (rename the source's single group, or wrap bare graphics), stripping `gorn` and staying byte-stable.
  Both keyed by the layer xml-name string (like `PEConnector`), decoupled from `ViewLayer`. The live
  `PEMainWindow::createBlankPart()` / `importSvgFile()` File-menu actions build/normalize into
  `m_fzpDocument` + per-view svg files and `reload()`.

- **P7 — units + history + sanitize hardening. ✅ LANDED (Session 23).** Two halves, both pure +
  golden-tested plus live wiring:
  - **units** — `PEUnits` (`src/partseditor/peunits.{h,cpp}`, core+xml-only): `pxToUnits`/`unitsToPx`/
    `pxToUnitsStr` (the px↔user-unit math: `/90` for inches, `*25.4/90` for mm; unknown unit = identity
    passthrough). `PEUtils::convertUnits`/`unconvertUnits`/`convertUnitsStr` now *delegate* to it
    (behavior-identical; the inline math they used is now isolated into the testable pure core).
  - **move/resize command coverage** — `PEElement::moveBy(delta)` (translate a graphic's geometry:
    rect x/y, line x1..y2, circle/ellipse cx/cy, text x/y, every polyline/polygon point, and — re-encoded
    in the source's own abs/rel convention — each `<path>` anchor) and `PEElement::resizeRect(to)` (replace
    a `<rect>`'s box; defined for rect only, since the other kinds store a position, not a size). Both
    rewrite the DOM element in place. Live: new `PEMainWindow::moveGraphic`/`resizeGraphic` model ops
    (find-by-id → mutate → rewrite svg + fzp → re-init overlay) and `MoveGraphicCommand`/
    `ResizeGraphicCommand` (`pecommands.{h,cpp}`), pushed from `itemMovedSlot`/`resizedSlot`. A grip-drag on
    a graphic now rewrites the underlying SVG (undoable + persisted) rather than just moving the overlay.
    `PEPrimitive::kindForElement(element)` (public) recovers an element's kind from its tag so the live ops
    dispatch moveBy/resizeRect per kind. `test_pedocument` grew 27 → 37 (10 new P7 cases).

## F.5 The live-item rework — P8+ (the part that was missing; roadmap reopened)

P0–P7 built the *engine* (typed DOM model + commands + undo + units). It never became a *car*: the live
editor still renders a baked `QGraphicsSvgItem` and overlays invisible `QGraphicsRectItem` hit-targets
(`§C`/`§C′`). Per the §B.1 decision, we now replace the overlay with live editable items. Ordered slices:

- **P8 — transform context on the model (pure, golden). ✅ LANDED (Session 25).** `PETransform`
  (`src/partseditor/petransform.{h,cpp}`, pure + Qt-only) implements the §B.2(b) flatten-and-drop contract:
  `parseTransform` (parses `translate/scale/rotate/skewX/skewY/matrix` into a `QTransform`), `inheritedTransform`
  (folds the ancestor `<g>` chain + the element's own `transform` into one `QTransform`), `bakeInto` (rewrites
  the element's own geometry attrs with the transform mapped in — rect/image via `mapRect`, line/polyline/polygon
  per point, circle/ellipse center + radii, `text` anchor, `<path>` re-encoded to absolute), and `flatten`
  (bake + drop the `transform` attribute). `test_pedocument` 37 → 45 (8 new P8 cases). No live behavior change
  yet — wiring into `initSvgTree` is P9.
- **P9 — flatten-and-drop wired into the live surface. ✅ LANDED (Session 25).** `PEDocument::flattenInPlace()`
  (pure) bakes each *leaf* graphic's nearest `<g>` transform into its own attrs (absolute) and drops it;
  `initSvgTree` now calls `typed.build(doc); typed.flattenInPlace();` before building overlays, and
  `getPixelBounds` reads the element's own (now absolute) attrs directly instead of the old
  renderer-bounds-then-scale-to-pixels. Because both the renderer and the overlay's `setPos(itemBase->pos()
  + topLeft)` live in the svg's local/user-unit space, this *closes* the §B.2 latent bug (previously the
  overlay was scaled to pixels while `moveBy` rewrote user-unit attrs → a graphic under a transformed `<g>`
  snapped when dragged). Groups keep their `<g transform>` (flatten is leaf-only) so group-as-unit and the
  renderer's per-id addressing keep working; self-rendering per-kind items are P10 (landed below).
  `test_pedocument` 45 → 47 (2 new `flattenInPlace` cases).

- **P10 — live self-rendering item + command rewire. ✅ LANDED (Session 25).** `PETransform::shapeFromElement`
  (pure) decodes a *modeled leaf* element into a `QPainterPath` in the item's local (absolute) coords;
  `PEGraphicsItem::paint` now renders that path with the element's own `fill`/`stroke`/`stroke-width`
  (defaulting to no-brush/no-pen when absent/`none`), replacing the old baked-`QGraphicsSvgItem`-under-hit-rect
  overlay. The path decode is byte-consistent with the rest of the app: it reuses `SVGPathLexer`/`SVGPathParser`
  (H/V normalized upstream) and `SVGPathRunner::commandSignal`, dispatched to a file-local `PEPathShapeAdapter`
  (`Q_OBJECT`, connected via the `SIGNAL`/`SLOT` string form like `svgfilesplitter.cpp`); the meta-object is
  moc'd from `petransform.cpp` (`#include "petransform.moc"`), NOT a header (a `Q_OBJECT` header re-triggers the
  AGENTS.md §5 aggregate build breakage). The item's signals (`terminalPointChanged`/`pathAnchorChanged`) feed
  the existing `MoveGraphicCommand`/`ResizeGraphicCommand`/`EditPathAnchorCommand` (no new command layer — signal
  source changed, commands stay). `test_pedocument` 47 → 55 (8 new `shapeFromElement` cases: rect/circle/ellipse,
  line hairline, polygon/polyline point-list, path abs+relative, cubic tight-bounds, unmodelled→false, plus a
  live-render round-trip proving the shape follows a `moveBy`).
  **Documented limitation:** Qt 6 `QPainterPath` has no close-segment element type, so a `<polygon>` is modeled
  as an open point-list (identical to `<polyline>` over the same points); close-state is not observable.
- **P11 — entry + discoverability. (planned)** Make the editor the Welcome landing ("New part" → straight onto the
  canvas); add a toolbar "New part"/"Open part" affordance so authoring a footprint is the front door, not
  a right-click.

**Deferred follow-ups** (explicit non-goals this pass): numeric-coordinate input field (the model already
supports coordinate edits via `moveGraphic`/`resizeGraphic`; only grip-drag is wired), a live unit-toggle
UI (`PEUtils::Units` is still a fixed `"mm"`), group-as-unit selection (see §B.2 cost), and a lossless path
re-encode that preserves each segment's original abs/rel encoding (P8 normalizes every coordinate to
absolute — geometry is exact; encoding normalization is the one lossy-in-text-but-true-in-geometry trade).

## G. Open design decisions
- **RESOLVED (Session 24, §B.1):** the live canvas uses real editable `QGraphicsItem`s, not the static
  `QSvgRenderer` preview + overlay hit-rects. **§B.2 resolved as (b) flatten-and-drop-at-load**, landed as
  pure `PETransform` (P8), wired into `initSvgTree` (P9), and made self-rendering (P10). Group-as-unit
  selection remains deferred (see the §B.2 cost note).
- Freehand capture: polyline-of-segments vs auto-smooth (match svgedit: store as `L`-segments until edited).
- Gorn (`generic:origin`) handling: current save does `removeGorn(...)`; model must round-trip it.

## H. Status
- **Now (Session 28): M0 stabilization started; two real defects found and fixed.**
  1. **Destructive load-time flatten (data loss).** `initSvgTree()` ran `PEDocument::flattenInPlace()` on
     `*viewThing->document`, which *is* `m_breadboardDocument`/`m_schematicDocument`/`m_pcbDocument`/
     `m_iconDocument` (`pemainwindow.cpp:451-463`) — the very DOMs serialized on save (`:2471`, `:2187`,
     `:1726`). Merely opening a part and saving it rewrote the library file, **lossily**: a `<circle>` under
     a non-translation loses its `r` (`bakeInto` documents this), every `<path>` is re-encoded to absolute,
     and every ancestor `<g transform>` is destroyed. It also ran outside the undo stack, so it could not
     be undone. **Fixed:** the transform is now carried per item (`PEGraphicsItem::setAbsoluteTransform`)
     as display/hit-test state alongside `m_offset`/`m_docBounds`; geometry attrs stay local, the document
     round-trips byte-for-byte, and groups keep their transforms (keeping group-as-unit selection possible).
     `flattenInPlace()` is retained for pure model tests only and documented deprecated-for-live-use.
  2. **`PETransform::inheritedTransform()` composed the chain backwards.** It walked the ancestor chain
     child→root while accumulating `result = result * t`, applying the **outermost** transform first and the
     element's own transform **last**. SVG nesting semantics are `abs = G_outer * … * G_inner * own * p`, so
     each newly-encountered transform must be **pre-multiplied**. Ground truth for
     `translate(10,20) > scale(2) > rect rotate(90)`: `(1,1) → (-42,22)`; the old code produced `(-22,12)`.
     Every existing P8/P9 golden used single-level nesting, where order is irrelevant — which is exactly why
     this was never observable. **Fixed** and pinned by a ground-truth test.
- **§B.2 is now RESOLVED AS (c), superseding the (b) flatten-and-drop decision:** keep the model document
  pristine (local attrs + intact group transforms) and carry the resolved transform as *item* state.
  (b) was rejected because it is lossy and because it silently made `moveGraphic`/`resizeGraphic` correct
  only by construction — those apply a user-unit delta straight to the element's own attrs, which is valid
  only if the transform was destroyed. With (c), callers map an absolute delta through
  `PEDocument::absoluteTransform(id)` (see the `m02_move_is_local_space_so_callers_must_map` contract test).
- **Library reality check (corrects an earlier assumption in this doc):** only **109 of 9,128** part SVGs
  use a `<style>` block — the library is overwhelmingly inline-attribute. A CSS cascade is therefore **not**
  the blocker for rendering existing parts; it matters for the 1.2% tail and for imported SVG. Correct
  inline rendering, non-destructive transforms, real Text, and the stale-item bug are.
- **Verification correction:** the "aggregate suite is green" claim must be checked with a **serialized**
  build. Concurrently racing `make -j` runs in this environment produce ~180 spurious link errors
  (`undefined reference to TextUtils::*`, `vtable for …`, `undefined reference to main`) with byte-identical
  link lines and suites that pass in isolation — i.e. it *looks exactly like* the AGENTS.md §5 path-keying
  hazard but is memory/resource contention. Verify with `make -j1` before believing a §5 regression.
- **Next:** finish M0 (stale-`PEGraphicsItem` cleanup in `initSvgTree`, null-element `makePegi` guard in
  `itemAddedSlot`), then M1 (selection persistence, one coordinate space, real Text, inline style fidelity,
  drag-to-create, resize-all-kinds, headless mouse-event coverage).
- **Engine (P0–P7) reused** (PEDocument/PEElement/PEPath/PEPrimitive/PEConnector/PEUnits + pecommands).
  Branch `feature/vector-editor`. `test_pedocument` now **60/60**.
- **Next:** P11 (Welcome landing + toolbar entry). Deferred: numeric-coordinate field, live unit toggle,
  group-as-unit selection, lossless per-segment path abs/rel re-encode.
- **Earlier (engine) slices landed:** P7 units+history+sanitize (S23), P6 import/blank (S22), P5 library-at-hand
  (S21), P3+P4 (S20), P2+P3 live (S19), P3 model (S18), P2 (S16–17), P1 (S15), P0 typed model (S14).







