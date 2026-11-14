# Parts Editor — feature roadmap (the "make it useful" build file)

> **Read me first (fresh session).** The sole-renderer Parts Editor spine is DONE and green: it renders each view's
> SVG through the self-paint itemizer, and select/move/undo/redo/save round-trip. **R1 resize-that-actually-resizes is
> DONE and committed/pushed.** What is missing now is the remaining *tooling* that makes a real vector editor useful:
> a **measurement/units system**, **drag-in alignment guides**, **zoom (slider + wheel)**, and a **create/toolbox** to
> add shapes and pull in existing shapes/parts. Work the remaining slices in order; each lands green + golden-tested +
> a named manual check, committed as its own slice (AGENTS.md §4). Do NOT regress the raw-source-of-truth model or the
> no-double-paint guarantee.

---

## 0. What already exists (do not rebuild this — build ON it)

Current sole-renderer surface (`src/partseditor/`), all green on `feature/vector-editor` and committed/pushed:
- **`PESvgEditor`** (`pesvgeditor.{h,cpp}`) — the editing widget. Owns a `QDomDocument m_doc` (the sole model), a
  `QGraphicsScene`/`PESvgEditorView`, a selection marquee + 8 resize grips (only for `isResizable` kinds; a circle/
  path shows no resize grips) + path-anchor node grips. Interaction entry
  points (public, controller-testable): `handlePress/handleMove/handleRelease`, `undo/redo`. `setDocument(doc,label)`,
  `setVisibleLayers(list)`, `fitToWindow()`, `saveToPath(path)` (Slice F; raw bytes, no colour leak), `saved()` signal,
  `dirtyChanged`, `undoRedoChanged`, `elementSelected(id)`, `selectionInfo(text)`.
- **`PEItemPlan::plan(root, visibleLayers)`** (`peitemplan.{h,cpp}`) — the pure itemization: an id'd primitive with
  bounds → `{id, editable:true}` (its own selectable `PESelfPaintItem`); a group of opaque id-less art →
  `{gid, editable:false}` (renders whole, not selectable). Because a group is whole-OR-descended (never both), nothing
  paints twice.
- **`PESelfPaintItem`** (`peselfpaintitem.{h,cpp}`) — paints exactly one planned unit's decoded `QPainterPath` at the
  identity transform; `hitTestId(scene, point)` → smallest-area id.
- **`PEPrimGeom`** (`peprimitivegeom.{h,cpp}`) — per-kind DOM geometry: `kindFor`, `kindName`, `moveBy(el,delta)`,
  `resizeTo(el, from, to)` (generic: rect/ellipse/line/polyline/polygon; circle/path are NOT box-resizable),
  `isResizable(el)`, `localBounds(el)` (min/max point box; normalized), `findById`, `pathAnchors(el)`, `editPathAnchor(el,i,pt)`.
- **`PEPath`** (`pepath.{h,cpp}`) — `<path d>` decode/encode, relative-aware, stable round-trip.
- **`PETransform::flatten(el)`** (`petransform.{h,cpp}`) — bake ancestor transforms into absolute attrs at load;
  `shapeFromElement` is the self-paint shape source.
- **`peunits.{h,cpp}`** — **already** converts px↔in/mm at 90 dpi (`SvgDpi=90`, `pxToUnits`, `pxToUnitsStr`). Wire it
  into the UI (R2).
- **`PEViewWriter::writeViewSvg(doc, path)`** (`peviewwriter.{h,cpp}`) — atomic serialize on save.
- **`PESvgPartLoader`** (`pesvgpartloader.{h,cpp}`) — `loadPart(fzp/path, View)` returns the **RAW** svg (no recolour;
  colourise is dead on the load path). `View` = {PcbView, BreadboardView, SchematicView}.
- **`PEMainWindow`** (`pemainwindow.{h,cpp}`) — the app-side shell. `init()`/`initSketchWidgets` build a central
  `QTabWidget`: **one `PESvgEditor` per view** (order `m_viewOrder` = Breadboard, Schematic, PCB, Icon) + reused
  **Metadata** (`PEMetadataView`) and **Connectors** (`PEConnectorsView`) tabs. `setInitialItem(paletteItem)` reads the
  `.fzp` into `m_fzpDocument`, then `loadViewDocument(viewID)` reads each `<viewName><layers image="fam/xxx.svg">`
  (resolved against `<partsroot>/svg/<family>`) into `viewDocument(viewID)` and `editor->setDocument(...)`. `saveAs`
  serializes each editor's live doc via `PEViewWriter` and repoints `<layers image>`; fzp save-gate does history+version.
- Golden suite `tests/auto/test_pesvgeditor/test_pesvgeditor.cpp` (**40 tests, QTEST_MAIN, `-platform offscreen`**):
  colourise, geom kind/move/resize/find, path decode/anchor, plan (no-dup, opaque-whole), self-paint (paints-only-own,
  selectable-leaf, subtree-order), pipeline (nested copper once, flatten absolute), interaction (smallest-area select,
  move+undo round-trip), chrome (rect grip round-trip, path node round-trip), save (round-trips, fails w/o path).
- Launch path: `MainWindow::openNewPartsEditor(paletteItem)` (`src/mainwindow/mainwindow_menu.cpp`) → `new
  PEMainWindow` → `init` → `setInitialItem`. Standalone tool: `tools/peditor/main.cpp` (File open/Save/Save As, Undo/
  Redo, `PED_SHOT` headless capture).
- **Legacy is gone** (Slice E): `PEGraphicsItem`/`PEBlankPart`/`PEDocument`/`PESvgImport`/`PEToolView` deleted; the
  whole old cluster is parked (reference only) in `src/partseditor/obsolete/` (`resizablerectitem`, `cornerhandler`,
  `partseditorview*`, `svgview`, `pcbxml`, …). Never re-wire those against the new path — port the *ideas*.
- **View→xml mapping** (`src/viewlayer.{h,cpp}`): `IconView→"iconView"`, `BreadboardView→"breadboardView"`,
  `SchematicView→"schematicView"`, `PCBView→"pcbView"` (`viewIDXmlName`). Layer groups carry ids like `copper0`,
  `copper1`, `schematic`, `breadboardbreadboard`, `silkscreen`, etc.
- **Build/tests:** see AGENTS.md §5 (fresh build dir always; QtTest suites take `-platform offscreen`; Boost suites

---

## 1. Ground rules (carry forward from pe-integration-plan.md §0)

1. DOM is the single source of truth. Every edit mutates `PESvgEditor::m_doc` (by id) then `rebuildScene()`.
2. No `SketchWidget`/`ItemBase`/`ModelPart`-reload round-trip for the graphic. `ModelPart`/`ReferenceModel` only locate
   the `.fzp` (path/moduleId/properties).
3. Raw-source-of-truth: never bake a *view* colour into the saved svg. Geometry edits persist; colours stay authored.
4. No-double-paint is sacred: any new create/insert path must feed `PEItemPlan` so each element is painted exactly once.
5. No silent fallbacks: an unsupported edit is a no-op with an honest status message, never a corrupt write.
6. Golden-test the pure logic (geometry, plan, units, snap math), not just smoke.
7. Land small; each slice green + manual-checked before the next.

---

## 2. Slices (each = build green + golden tests + named manual check + its own commit)

### R1 — Resize that actually resizes  **[DONE]**
**Root cause (confirmed, not guessed).** `pesvgeditor.cpp` `handleMove` treated a grip-drag exactly like a body-move
(translate via `setPos(delta)`), so a corner-drag slid the shape instead of growing it from the grabbed edge. A *second*
latent bug compounded it: `PEPrimGeom::localBounds` for polyline/polygon accumulated per-point boxes with
`QRectF(tl,br).normalized()` where tl==br — a zero-size rect — and `united()` of those stays degenerate, so a polygon's
box came back invalid and any resize silently refused. (Measured against real Qt: `QRectF.isValid()` is false whenever
width **or** height is 0, and `united` of equal-point rects does not widen.) Fixed by computing min/max over all points.
**Fix (landed).**
- `PEPrimGeom` gained a **generic** `resizeTo(el, from, to)` (rect→x/y/w/h; ellipse→box extents; line→endpoints;
  polyline/polygon→scale the point list from `from` to `to`) and `isResizable(el)`. The rect-only `resizeRect` is
  **removed** (its behavior is now `resizeTo`'s Rect case).
- **Design decision (user):** a `<circle>` stores a single `r` — there is no honest per-axis box resize — so it is **NOT**
  resizable (`isResizable` false → no grips drawn; body-drag still moves it). A `<path>` is node-edited per-anchor via
  `editPathAnchor`, never box-resized. `resizeTo` routes Circle/Path/Group/Unknown/Text to an honest `false`.
- `pesvgeditor` `handlePress`/`updateSelectionVisual` gate grips on `isResizable(sel)`; `handleMove` **live-writes** the
  resized box into the DOM (re-bind item + redraw) instead of translating; `handleRelease` commits idempotently;
  `UndoEntry{Resize, from, to}` replays/reverses via `resizeTo(el, localBounds(el), e.to/from)`. Grips stay
  zoom-independent (`kGripPx / viewScale`).
**Golden tests (green):** `geom_resizeRect` (rect via `resizeTo`), `geom_resizeToLine`, `geom_resizeToPolygonScalesPoints`
(scales only the grabbed axis), `geom_resizeToCircle` (asserts **not** resizable / no-op), `geom_resizeToRejectsPath`,
`controller_circleIsNotResizableNoOp` (body-drag moves, `r` unchanged), and `chrome_rectGripResizeRoundTrip` (select →
grab corner → width/height grow from that corner, x/y pinned; undo/redo replay). Manual: select a rect/polygon, drag a
grip → it resizes live from that edge; a circle shows no grips and only moves; a path shows node grips.

### R2 — Measurement / units system
**Reuse** `peunits.{h,cpp}` (already px↔in/mm @ 90 dpi).
**Build.**
- A unit preference (in / mm / mil; default mm) as a real preference (pin it in tests — AGENTS.md §4).
- Status bar / readout showing the **selection** origin+size and the **cursor** position in the chosen unit (convert
  the current `selectionInfo` px values through `pxToUnits`).
- A visible coordinate/size readout near the selection (a small always-on panel or the existing `m_svgLabel`).
- Optional: a ruler along the top/left in the chosen unit (later; mark as sub-slice).
**Golden tests:** `pxToUnits` conversions already partly covered; add: selection info formats a known px box to the
expected mm/in/mil string; unit switch changes the readout deterministically.
**Manual:** open a part, select a pad → readout shows the pad's w×h in mm and in inches correctly; switch unit → updates.


### R3 — Drag-in alignment guides / snapping
**Build (pure + golden-testable).** A `PEAlign` (or extend `PEPrimGeom`) pure function: given the dragged element's box
and the set of sibling element boxes (or a grid), return snap deltas + guide lines:
- edge-align (left/right/top/bottom) and center-align (cx/cy) to any sibling within a tolerance;
- equal-gap / equal-spacing between three in a row (later);
- optional grid snap (grid size in px, from the units preference).
Wire it into `handleMove`/`handleRelease` for both move and (optionally) resize; draw the guide lines as scene chrome
(zoom-independent pen), clear on release.
**Golden tests:** given two boxes, snapping aligns the moved one's left edge to the other's and returns a guide line;
center-align case; no-snap when beyond tolerance. Manual: drag a pad so its top aligns to another pad → a guide line
appears and it snaps; drag away → guide clears.

### R4 — Zoom: slider + wheel
**Build.**
- `PESvgEditorView::wheelEvent`: Ctrl+wheel (or plain wheel — pick one, document it) zooms about the cursor
  (`AnchorUnderMouse` is already set). Keep `kGripPx/viewScale` chrome sizing correct; clamp min/max scale.
- A **zoom slider** (QSlider) in the editor chrome bound to scale (log-ish scale for even feel), + buttons
  (zoom-in/out/fit/100%). Expose `zoomIn()/zoomOut()/setZoom()/zoom()` on `PESvgEditor`.
- Zoom must not corrupt geometry (geometry is px; only the view transform changes).
**Golden tests:** programmatic `setZoom` changes `view->transform().m11()` and grip hit-tolerance tracks scale
(`hitResizeGrip` still hits); `fitToWindow` recomputes after content change. Manual: wheel zooms at cursor; slider
zooms; grips stay grabbable when zoomed far out/in.

### R5 — Create/toolbox: add shapes + pull existing shapes/parts
This is the big "Fritzing fluff". Two complementary abilities:
**5a. Create-from-scratch primitives (DOM-native).** A toolbox panel (dock or toolbar) with tools: rect, circle,
ellipse, line, path (pen), text. Selecting a tool + drag/click on the canvas **appends a new element into the current
view's `m_doc`** under the correct layer group (respect `visibleLayers` + `isLayerGroupId` confinement), assigns a
unique `id`, and it immediately participates in `PEItemPlan` (so it renders once and is selectable/movable/resizable
via R1). Add/delete supported via a command (`pecommands.cpp`) so it is undoable + drives `dirtyChanged`.
- Reuse: the shape semantics from `obsolete/resizablerectitem`/`cornerhandler` (as reference only), `PEPrimGeom` for
  the created element's geometry, existing `pecommands.cpp` for undoable ops.
**5b. Pull from existing shapes/footprints/parts (library-at-hand).** A browse/search panel over the bundled parts
tree (`<partsroot>/{core,contrib,user}` + `svg/<family>`) that lets the user pick an existing part/footprint and insert
its graphic (that view's svg subtree) into the current view DOM (as a nested `<g>` with a fresh id), preserving colours
(raw). This is "easy placement of already existing shapes/footprints/part base".
- Search = moduleID/title/properties; show thumbnails (render via the same self-paint path or a `QSvgRenderer`
  thumbnail — thumbnails may use QtSvg since it's a separate non-editable render, fine).
**Golden tests:** create a rect via the tool API → appears once in `PEItemPlan`, selectable, moves/resizes; create-
undo removes it; inserting an existing part's `<g>` yields exactly one planned unit and no colour baked; id uniqueness.
**Manual:** toolbox → click rect, drag on canvas → a rect appears and is editable; pick a library footprint → it drops
in and is selectable; undo removes.


### R6 — Metadata + Icon blank; Schematic not showing (root cause + fix)
**Root cause (found).** `PEMainWindow::setInitialItem(nullptr)` (no palette item) falls back to
`m_referenceModel->retrieveModelPart("generic_ic_dip_8_300mil")` — **that moduleId is NOT in the bundled tree**, so the
default launch loads nothing → Metadata blank + Icon tab has nothing. (When a real part is opened, all four `<...View>`
load via `loadViewDocument`.)
**Fix.**
- Pick a **real** default moduleId that exists (e.g. `generic_sip_2_300mil`) for the blank-new-part fallback, OR make
  "new part" create a blank-but-valid fzp with all four views so Metadata has fields. Confirm the chosen default has a
  `.fzp` + all four `<layers image>` svgs present.
- Verify `setInitialItem`→`loadViewDocument(SchematicView)` actually resolves the schematic svg path and
  `editorFor(SchematicView)->setDocument(...)` is called with a non-null doc; if schematic is blank even for a real
  part, check the `<layers image>` for schematic resolves (path math `fzpDir + "/../svg/" + family` + `shortPath`).
- Confirm Icon tab: some parts legitimately have a minimal icon; if blank for a real part, that's a real bug — else it
  was just the bad-default-part artifact of R6.
**Golden tests:** `loadViewDocument` for a sample part populates each `viewDocument(viewID)` and each editor's doc;
schematic content is non-empty for a part that has a schematic svg. Manual: open the editor on a real part → Metadata
tab shows editable fields; Icon tab shows the icon; Schematic tab shows the schematic graphic.

---

## 3. Definition of done for "editor is useful"

- ✅ Resize resizes (all supported kinds), live preview, undoable.
- Units in/mm/mil readout for selection + cursor; unit switchable + pinned in tests.
- Drag-in alignment guides + snap (edge/center, optional grid), golden-tested.
- Zoom via wheel (at cursor) + slider; chrome stays zoom-independent.
- Toolbox creates rect/circle/ellipse/line/path/text into the DOM (undoable) and inserts existing part graphics.
- Metadata tab populated; Icon renders; Schematic renders (for parts that have one).
- Full suite green from a fresh dir; no double-paint; raw colours preserved; each slice committed separately.

## 4. Order of attack (suggested)

R1 is done. Remaining: R4 (zoom) → R2 (units) → R3 (alignment) → R6 (blank tabs fix — unblocks real-part QA) → R5
(toolbox, the biggest). R4/R2 are quick wins that make manual QA of the rest possible, so do them next. Everything is
on `feature/vector-editor`; commit each remaining slice as you go (Conventional Commits, `feat(partseditor):`/
`fix(partseditor):`/`test(partseditor):`).

## 5. Gotchas / hazards to not repeat

- **Relative include:** app sources include cross-dir headers as `#include "../utils/textutils.h"` (bare include broke
  the app build). Any new `src/partseditor/*.cpp` crossing into `src/utils`/etc. must use the relative form.
- **qmake path-keying** (AGENTS.md §5): list every source/header in a suite `.pro` with explicit `$$absolute_path()`;
  test class `Q_OBJECT` lives IN the `test_*.cpp` (so the `.moc` rule keys). Do not mix globs + absolute entries.
- **0-byte-object / stale build:** always rebuild from a fresh dir; a killed `make` leaves truncated 0-byte objects that
  mislead. Check `stat -c%s *.o` (a real TU is hundreds of KB, not 0).
- **QtTest offscreen raster is flaky** for some captures — use `QT_QPA_PLATFORM=xcb` + `Xvfb :99` for visual QA;
  `PED_SHOT=<png>` on `tools/peditor` renders a PNG once for diffing.
- **Do not** reintroduce the QtSvg-isolation double-render; keep self-paint per planned unit.
- Nested copper confinement: editing a leaf under `copper0`/`copper1` must not disturb the sibling layer group.
