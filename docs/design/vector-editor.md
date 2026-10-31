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

## C. Ground truth (what exists today)

- The **live** parts editor is the `pe*` family: `src/partseditor/pemainwindow.cpp` (+
  `pemetadataview`, `peconnectorsview`, `pesvgview`, `petoolview`, `pegraphicsitem`,
  `pecommands`). The *old* editor (`partseditormainwindow`, `partseditorview`, old `svgview`)
  is parked in `src/partseditor/obsolete/` and **commented out** in `pri/partseditor.pri`.
- **It has never been a vector editor.** `initSvgTree()` (`pemainwindow.cpp:1393`) parses the
  part's SVG into a `QDomDocument`, normalizes it via `FSvgRenderer::loadSvg(...)`
  (`FSvgRenderer : public QSvgRenderer`, Qt *static* SVG; returns cleaned SVG bytes), then does a
  depth-first walk (`pemainwindow.cpp:1444`) and creates **one `PEGraphicsItem` per element**.
- `PEGraphicsItem` (`pegraphicsitem.h`) is a **`QGraphicsRectItem` overlay** — a hit-target rect
  over an already-baked SVG node. Its only affordances are hover/press/move and a single
  **terminal-point** handle. There is **no primitive creation and no node/path editing**: grep of
  the live `pemainwindow.cpp` for `addRect`/`addPath`/`addText`/`insertElement`/`createNew` → none.
- Rendering into the canvas is `QGraphicsSvgItem` + `QSvgRenderer` (`sketchwidget.cpp:3230,9882`).
- Round-trip: on save the DOM is serialized back with `removeGorn(...)`/`writeXml(...)`
  (`pemainwindow.cpp:907,1716,2177`); the `QDomDocument` is the source of truth.

Consequence: today, to author *any* new part graphic you must leave the app (real Inkscape) or
bake a symbol with the parametric `fritzing-schematic` plugin. That is the gap this work closes.

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

## G. Open design decisions
- Freehand capture: polyline-of-segments vs auto-smooth (match svgedit: store as `L`-segments until edited).
- Static `QSvgRenderer` stays the *preview*; the live-edit canvas must use real `QGraphicsPathItem`s.
- Gorn (`generic:origin`) handling: current save does `removeGorn(...)`; model must round-trip it.

## H. Status
- **Now:** **P7 units + history + sanitize landed** (Session 23) — pure `PEUnits` (px↔user-unit math that
  `PEUtils` now delegates to) and pure `PEElement::moveBy`/`resizeRect` (per-kind geometry rewrite,
  path anchors re-encoded in the source's abs/rel convention) with live `PEMainWindow::moveGraphic`/
  `resizeGraphic` ops + `MoveGraphicCommand`/`ResizeGraphicCommand` so a grip-drag on a graphic rewrites
  the SVG undoably (not just the overlay). `test_pedocument` 27 → 37. App builds; all 12 suites green.
  **This completes the P0–P7 roadmap** as scoped.
- Prior: **P6 SVG import + create-from-blank** (Session 22) — pure `PEBlankPart`/`PESvgImport` + live
  File-menu actions. **P5 library-at-hand** (Session 21) — PE dock constructs `BinManager` +
  `initStandardBins()` (all bins + Search). **P3 trigger + P4 connector semantics** (Session 20).
  **P2 + P3 live wiring** (Session 19). **P3 model** (Session 18). **P2** (Sessions 16–17). **P1**
  (Session 15). **P0** (Session 14). Branch `feature/vector-editor` (pushed to `landracer`).
- Next: roadmap **complete** (P0–P7 landed). Follow-ups to consider when a concrete need arises: a live
  unit-toggle UI wired to `PEUnits` (the toggle is not yet exposed in `PEToolView`/`PESvgView`); a live
  numeric-coordinate *input* field (the model supports it via `moveGraphic`/`resizeGraphic`; only the
  grip-drag trigger is wired today).







