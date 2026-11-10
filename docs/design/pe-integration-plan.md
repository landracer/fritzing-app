# Parts Editor integration plan — new sole-renderer, old shell (rewrite, not retrofit)

> **Read me first.** Concrete plan for the next task: wire the clean `PESvgEditor` sole-renderer (see
> `docs/design/pe-sole-renderer.md`, steps 1–5 proven standalone in `tools/peditor`) into a real Parts
> Editor that looks/behaves like the old one — **but is rebuilt around the new renderer, using the old
> code as reference only.** So a fresh session starts unambiguous and does not re-import the legacy
> `SketchWidget`/`ItemBase` breakage.

## 0. Ground rules (anti-BS contract)

1. **Old code is reference, not dependency.** Reuse only the seams proven to be clean DOM logic (§2). Do
   **not** route through `SketchWidget` / `ItemBase` / `ModelPart` / `PEGraphicsItem` / the
   `itemMoved/resized/itemAdded/newWire` signal web. Those are the broken garbage; absent from the new PCB path.
2. **One source of truth.** `PESvgEditor`'s `QDomDocument` is the sole model of the view graphic. No second
   renderer, no backdrop, no parallel `PEElement`/`PEGraphicsItem` overlay for the PCB view.
3. **Every slice lands green and is verified before the next.** App builds, `test_pesvgeditor` (+ any new
   suite) green from a fresh dir (AGENTS.md §5), plus a real-app/manual check of that behavior.
4. **No silent fallbacks.** A not-yet-supported edit is a no-op with an honest message, never a corrupt
   write (cf. `PEPrimGeom::moveBy` returning false for `<path>`).
5. **Standalone-first.** The new editor stays drivable from `tools/peditor` until proven, then the shell
   hosts it. Do not tear down the old PE until the new one replaces it slice-for-slice.

## 1. Target architecture

A `PEMainWindow`-shaped window (same docks + tabs as today: **Library/Bins**, **Inspector**, **SVG**,
**Connectors**, **Layers** docks; **Metadata**, **Connectors** tabs) whose *central view area* hosts
`PESvgEditor` (PCB first, then breadboard/schematic via `PESvgPartLoader::View`). The dock/tab *panels*
are reused because they are clean DOM widgets. The render+edit+interaction spine is `PESvgEditor` +
`PEPrimGeom`. Persistence is a **new DOM-writer** (§4): the editor's `QDomDocument` is serialized straight
to the view `.svg`, and the `.fzp` is updated via the *existing* clean helpers (`loadFzp`, `saveFzp`,
`readHistory`/`writeHistory`). `SketchWidget`/`ItemBase` never load the part.

## 2. Keep-vs-rewrite boundary (explicit)

### REUSE (clean — verified no `SketchWidget`/`ItemBase`/`ModelPart` coupling)
- **`PEDocument` / `PEElement`** (`pedocument.{h,cpp}`) — 0 references to the broken layer; round-trip
  stable, unit-tested. Use as *reference* for geometry semantics; do not reintroduce the double-render.
- **`PEMetadataView`**, **`PEConnectorsView`** — DOM-in/DOM-out metadata + connector editors. Drive them
  from `m_fzpDocument` exactly as today.
- **`BinManager`** (library-at-hand), **`HtmlInfoView`** (Inspector) — reusable docks.
- **Save/fzp helpers:** `loadFzp(path)`, `saveFzp()` (writes `m_fzpDocument` via `writeXml`, returns path),
  `readHistory`/`writeHistory`, `bumpedVersion`, the save-gate `PEHistoryEntryDialog` flow. Reuse the
  *helpers*, not the `SketchWidget` model round-trip that currently feeds them.
- **`LayerPalette`** — show/hide layer groups; in the new renderer it toggles item visibility by layer id.

### REWRITE / REPLACE (the broken layer — do NOT carry forward)
- The `SketchWidget`-based "ViewThing" central views and their `itemMoved/resized/itemAdded/newWire`
  wiring (`pemainwindow.cpp` initViews, ~lines 470–520).
- `PEGraphicsItem` self-paint overlay and the reload/colourise-on-load mutation of `m_pcbDocument`.
- `PEToolView`'s coupling to `SketchWidget` (keep its *request signals* — create primitive, add connector —
  but re-implement their handlers against `PESvgEditor`/DOM, not `SketchWidget`).

### NEW (the seam between the two)
- **`PESvgEditor`** exists (standalone). In-app job: host in the central area, expose signals the docks
  need (`elementSelected(id)`, `selectionInfo(text)`), and accept `PEToolView` commands as DOM ops.
- **`PESvgEditorHost`** (new, thin): adapts `PESvgEditor` to the PE tab/dock signals — the *only* new
  coupling class. No business logic; pure signal routing + view↔fzp glue.
- **`PEViewWriter`** (new, the DOM-writer of §4): serialize the editor doc to the `.svg`, refresh `.fzp`.

## 3. Landable slices (each = build green + tests green + a named manual check)

**Slice A — shell hosts the renderer (no save yet).** New `PEViewHost` central widget: a `QTabWidget`
(PCB/Breadboard/Schematic) of `PESvgEditor`s, opened via `PESvgPartLoader::loadPart`. Stand up the reused
docks (Library/Inspector/SVG/Connectors/Layers) + Metadata/Connectors tabs. `setInitialItem`/`loadFzp`
still populate `m_fzpDocument`; the PCB tab shows the colourised DOM render. *Check:* open a core part in
the in-app PE, all three views render, docks populate, selection reports id/geometry.

**Slice B — selection ↔ Inspector/Connectors.** `PESvgEditor::elementSelected(id)` → highlight the
matching connector row in `PEConnectorsView` and its geometry in the Inspector. Reverse: clicking a
connector row selects + scrolls to its pad. *Check:* click a pad → connector row lights; click row → pad
outlined.

**Slice C — the DOM-writer (`PEViewWriter`), save lands.** On save: serialize each view's `QDomDocument`
to its `.svg` on disk (atomic `QSaveFile`), then `saveFzp()` + save-gate (history/version) via the reused
helpers. `.fzp` `<layers image>` stays pointed at the written svg. *Golden test:* synthetic DOM →
`PEViewWriter` → bytes on disk match an expected svg (attributes/order preserved), `.fzp` version bumped.
*Check:* move a pad, save, reopen from disk, pad is where you left it; version bumped; history recorded.

**Slice D — PEToolView create-primitive + add-connector, DOM-native.** Handlers rewrite `PESvgEditor`'s
DOM (append a `<rect>`/`<circle>` graphic; add a `connectorNpad` + the `.fzp` `<connector>` via the
existing connector helpers) instead of `SketchWidget`. Reuse `PEPrimGeom` for the new element's geometry.
*Check:* add a rectangle primitive + a connector; both render, save, and reopen intact.

**Slice E — resize + remaining primitive edits.** Implement `resizeTo` handles in `PESvgEditor` (the
deferred item): per-kind resize via `PEPrimGeom::resizeRect`/ellipse/line; wire `PEToolView` size fields.
*Check:* resize a rect/circle/line; save+reopen round-trips.

**Slice F — `<path>` move (and optionally shape-edit).** Wire the `PEPath` decoder so `<path>` elements
translate (currently a deliberate no-op). Keep it isolated; no fallback. *Check:* drag a silk `<path>`;
save+reopen.

**Slice G — breadboard + schematic parity.** Same host supports them via `PESvgPartLoader::View`; verify
edit+save for those views and reuse the connectors/metadata flows. *Check:* edit + save a breadboard and
schematic graphic.

**Slice H — retire the legacy path.** Remove the `SketchWidget`/`ItemBase`/`PEGraphicsItem` PCB branch
from `PEMainWindow` once A–G are green. Delete dead code; update `pri/partseditor.pri`; final full-suite
green from a fresh dir.

## 4. Save/persistence design (locked decision: new DOM-writer)

- **Single source of truth:** the `QDomDocument` owned by each `PESvgEditor`.
- **View svg:** `PEViewWriter::writeViewSvg(path, doc)` — serialize with the same XML conventions the
  existing `writeXml`/`PEDocument` round-trip uses (declaration, no spurious whitespace churn), written
  atomically via `QSaveFile`. Golden-tested on a synthetic doc.
- **fzp:** unchanged helpers — the editor does **not** own fzp serialization; it calls
  `PEMainWindow::saveFzp()` after any structural change and the existing save-gate handles history/version.
  `<layers image>` path is confirmed/rewritten to the just-written svg.
- **Non-goals:** no `ModelPart` reload, no `SketchWidget::loadPartFromClass`, no `PEElement` mirror.

## 5. Risks / watch-outs (the BS we are preventing)

- **Colourise-on-load must not be baked into the saved svg.** The saved `.svg` must stay in the part's
  original colours; colourisation is a *view-time* DOM transform (`PESvgPartLoader`), never written to
  disk. `PEViewWriter` writes the *un-colourised* doc. (A real prior bug class.)
- **QDom implicit sharing:** the editor mutates its own `m_doc`; ensure the writer reads that same tree,
  not a detached copy (we hit a readback confusion here before — pin it with a golden).
- **0-byte-object / stale-build traps:** always verify from a fresh build dir (AGENTS.md §5); a
  timeout-killed `make` leaves truncated objects that mislead.
- **Nested copper confinement:** editing a leaf under `copper0`>`copper1` must not disturb the sibling
  layer; keep the `isLayerGroupId` guard and the golden test green.
- **No double render:** `PESvgEditor` is the only painter for the part in-app; assert no `ItemBase` is ever
  added to that scene (worth a debug assertion).

## 6. Definition of done for "properly integrated"

App launches into the PE with the old layout, part loads from the library, PCB/breadboard/schematic render
via the sole renderer, select + move + resize + create + add-connector all work, save writes `.svg`+`.fzp`
(version/history), reopen round-trips, the legacy `SketchWidget` PCB branch is gone, and the full test
suite is green from a fresh build.
