# AGENTS.md — agent onboarding & working contract

> **Read me first.** This file is the entry point for any AI agent (or human) picking up
> this project in a fresh session. It tells you what this repo is, how it is built, how to
> verify a change, and the conventions this fork follows — so a new session becomes
> *project-aware* in minutes instead of re-discovering everything.

## 1. What this repo is

`landracer/fritzing-app` is a **fork-of-record** of the Fritzing EDA application (C++/Qt
electronic-design tool with a real-life "breadboard" view). It is the canonical, actively-maintained
line. Upstream `fritzing/fritzing-app` is a **read-only** remote we cherry-pick from — never push
to it (its push URL is disabled locally on purpose).

State-of-record at the time of writing:
- **Toolchain:** floating **Qt 6** (built/tested against Qt 6.11.2). The old `QT_MOST=6.5.10`
  ceiling was removed; only a floor (`QT_LEAST=6.5.3`) remains. The build keys off Qt *major*
  version, so it tracks modern Qt without a hard top-end cap.
- **Feature line:** `feature/qt6-modern-fixed` was promoted (fast-forward) into **`develop`** —
  the canonical line. It carries the Qt-6 float, the **panelizer** wizard + engine, the opt-in
  **Gerber preview**, and a fully green test suite.
- **Welcome landing page:** the app now opens on a Welcome tab (`src/sketch/welcomeview.*`)
  — recents list on the left, a live SVG-rendered **board preview** + gerber-production status on the
  right, and recents housekeeping (Remove / Purge-all). Opening a file loads into the *current* blank
  window (second file → new window). Full arc + learnings: `docs/internal/panelizer-progress.md`,
  Session 9.
- **Parts Editor sole-renderer (newest, standalone):** a clean PCB render/edit surface —
  `src/partseditor/pesvgeditor.*` (`PESvgEditor`), `pesvgpartloader.*` (loads a part via `.fzp`, colourises
  the DOM in place, nested-copper confined), `peprimitivegeom.*` (per-kind move/resize geometry),
  `peitemplan.*` (`PEItemPlan::plan` — the pure, headless-tested itemization decision) — driven by
  the standalone `tools/peditor` app. It renders via a **self-paint** model (Slices A–D, `peselfpaintitem.*`):
  each planned unit from `PEItemPlan::plan` is flattened (absolute geometry, identity transform) and bound to one
  `PESelfPaintItem` (a `QGraphicsObject`) that paints ONLY its own primitive(s) via `PETransform::shapeFromElement`
  — a group of editable id'd primitives is descended so each pad/pin is its own selectable item (`data(0)=id`);
  a group of opaque id-less art is rendered whole as one non-selectable item — never both, so no element is painted
  twice. `PESelfPaintItem::hitTestId(scene, point)` is the smallest-area selection. Editing is **attribute-level**
  via `PEPrimGeom`: body-drag → `moveBy(el,delta)`; a selected **resizable** primitive (rect/ellipse/line/polyline/
  polygon — `isResizable(el)`) shows a dashed marquee + **8 grips** (corners + edge midpoints) → the drag live-writes
  the resized box via generic `resizeTo(el, from, to)` (growing from the grabbed edge, not translating); a `<circle>`
  is **not** resizable (a single `r` has no honest per-axis box resize, so it shows no grips and only moves); a selected
  `<path>` shows each decoded **anchor as a node grip** → `editPathAnchor(el,i,point)` (a `<path>` is editable iff its
  anchors decode — `isEditableGraphic` fixed in Slice D so paths are selectable). Grip/node size is zoom-independent
  (`kGripPx / viewScale`). Undo is a tagged
  `UndoEntry` (Move/Resize/Anchor) replayed/reversed exactly. Select / drag-move / resize / node-edit / undo,
  bypassing (and, in Slice E, deleting) the legacy `ItemBase`/`PEGraphicsItem` double-renderer. **Not yet wired
  into `PEMainWindow`'s graphic-edit path** — the sole-renderer surface + selection/move/resize/node-edit
  interaction proven standalone (chrome golden-tested via the real controller). Design + status:
  `docs/design/pe-selfpaint-renderer.md` (current, §2b/§3b/§4) / `docs/design/pe-sole-renderer.md` (original);
  engine + interaction golden-tested in `tests/auto/test_pesvgeditor`. **Integration plan (rewrite-not-retrofit,
slices A–H): `docs/design/pe-integration-plan.md`** — the MainWindow-wiring entry point.
- **Parts Editor save + raw-source-of-truth (newest, Slice F):** the standalone `peditor` now has **File ▸ Save**
  (Ctrl+S, writes back to the opened file) / **Save As…** (Ctrl+Shift+S) and a **closeEvent** that prompts
  Save/Discard/Cancel when dirty. The editor exposes `PESvgEditor::saveToPath(path)` (serialises the owned
  `QDomDocument` via `toString(0)` and writes UTF-8 through `TextUtils::writeUtf8`, then clears dirty and emits
  `saved(path)`) and a `saved()` signal. **Raw-source-of-truth:** `PESvgPartLoader::loadPart` now returns the svg
  in its **raw on-disk bytes** — it does *not* recolour on load. Library svgs already author each layer's colour
  per layer-group (e.g. copper authored `stroke:#f7bd13`), so the raw DOM already displays correctly and
  `PEItemPlan`'s per-view layer filter selects what is shown. That is what makes Save a **lossless** round-trip:
  geometry edits persist in full and **no view colour is ever baked into the file** (the whole point — the raw file
  is the single source of truth; colourisation was only ever a display transform). The old `colouriseForView`
  display helper is retained (still golden-tested) but is no longer applied on the load path. Save is golden-tested
  in `test_pesvgeditor` (`save_roundTripsThroughDisk`, `save_failsWithoutFilepath`).
- **Parts Editor TOOLING roadmap (newest, the active front — read this to continue):** the sole-renderer spine
  (render/select/move/undo/save) is DONE; what remains to make the editor *useful* is tracked as landable slices in
  **`docs/design/pe-editor-roadmap.md`** — **R1** resize-that-actually-resizes (today a grip-drag translates instead of
  resizing), **R2** measurement/units (px↔in/mm/mil via `peunits`; not yet surfaced), **R3** drag-in alignment guides /
  snapping, **R4** zoom (wheel-at-cursor + slider), **R5** create/toolbox (add rect/circle/ellipse/line/path/text +
  pull existing library shapes/footprints/parts), **R6** Metadata/Icon blank + Schematic-not-showing (root cause: the
  no-palette-item fallback `retrieveModelPart("generic_ic_dip_8_300mil")` names a moduleId that **does not exist** in
  the bundled tree → blank default launch). All still on `feature/vector-editor`, uncommitted.
- **Tests:** all wired test suites build and pass on Qt 6.11.2 (see §5). `test_folderutils` was wired
  into `auto.pro` and the Boost-suite `.pro` files were converted to all-explicit `$$absolute_path()`
  source/headers (the path-keying hazard that made the aggregate `-j` build fail nondeterministically).
  `test_pesvgeditor` (QtTest, `QTEST_MAIN` — it links the widget/path-decode stack to raster-render the
  self-paint item) was added for the sole-renderer engine + interaction.

## 2. How the project is laid out

Top-level:
- `phoenix.pro` — the single qmake root. `TEMPLATE = app`, `TARGET = Fritzing`. Pulls in every
  `pri/*.pri` subsystem and sets the Qt floor / dependency detection.
- `pri/` — one `.pri` per subsystem (items, model, svg, simulation, dock, autoroute, gerberpreview,
  sketch, connectors, …) plus dependency-detection files (`quazipdetect.pri`, `boostdetect.pri`,
  `svgppdetect.pri`, `libgit2detect.pri`, `openssl3.pri`, `spicedetect.pri`, `clipper1detect.pri`).
- `src/` — application logic. Subsystem dirs mirror `pri/*.pri`:
  `src/autoroute` (PCB autorouter + the ported breadboard autorouter: `breadboard*.cpp`), `src/gerberpreview`, `src/svg`, `src/simulation`, `src/items`,
  `src/model`, `src/mainwindow`, `src/sketch`, `src/dialogs`, `src/referencemodel`,
  `src/partseditor`, `src/eagle`, `src/ipc`, `src/program`, `src/utils`, …
- `parts/` — the part library (metadata `.fzp` + `.svg`). **An embedded git repository** (not a
  submodule) pointing at `fritzing/fritzing-parts`.
- `tests/auto/` — the automated test suites, wired through `tests/auto/auto.pro` (see §5).
- `docs/` — the living documentation (this is the source of truth beyond this file):
  - `docs/panelizer.md` — user-facing panelizer guide.
  - `docs/design/panelizer-v2.md`, `docs/design/gerbview-port-reference.md` — design references.
  - `docs/design/part-migration.md` — the obsolete-part swap feature.
  - `docs/design/vector-editor.md` — spec for turning the parts editor into a de-facto parts/symbol
    design editor (library-at-hand + in-app vector editing + SVG round-trip; svgedit/Inkscape are
    references; phased P0–P11 — P8/P9/P10 landed the live self-rendering flattened surface, P11 pending).
  - `docs/design/pe-integration-plan.md` — the rewrite-not-retrofit plan for wiring the new `PESvgEditor`
    sole-renderer into `PEMainWindow`: explicit keep-vs-rewrite boundary, DOM-writer save design, and
    landable slices A–H. **Start the integration task here.**
  - `docs/design/pe-selfpaint-renderer.md` — **the actual fix for the PCB double-draw**, with proven root
    cause: QtSvg `render(id)` repaints the whole doc clipped to the bbox (proven this session), so per-element
    isolation is impossible and whole-group+leaf items always double-paint. Fix = self-paint each element
    (`PETransform::shapeFromElement` → fill/stroke the path), driven by `PEItemPlan`. Slices A–E (A–D landed
    as above; E deleted the entire legacy QtSvg-isolation cluster — `PEGraphicsItem`/`PESvgView`/`PEToolView`/
    `PEDocument`/`PEElement`/`PEPrimitive`/`PEConnector`/`PEBlankPart`/`PESvgImport` — now that `PEMainWindow`
    drives `PESvgEditor`. This is where the editor work actually goes now.
  - `docs/internal/panelizer-progress.md` — newest-first working log for the panelizer/gerber effort.
  - `docs/internal/issue-triage-log.md` — newest-first log of upstream issues we investigated and **shot
    down** (closed as FIXED / NOT REPRODUCIBLE / USER ERROR / WONTFIX / DESIGN LIMITATION), with root cause
    + evidence. Read this before assuming a reported issue is a real bug.
  - `docs/internal/feature-backlog.md` — decision-grade triage of deferred upstream feature PRs (what we
    landed vs ditched, and why).

## 3. Build & run (out-of-tree, always)

```sh
mkdir build && cd build
qmake6 ../phoenix.pro
make -j"$(nproc)"
./Fritzing
```

Runtime libs that must be present: Qt 6 (base/svg/serialport/5compat/tools), QuaZip-Qt6, Boost, libgit2,
OpenSSL 3, **ngspice shared lib** (searched in standard dirs like `/usr/lib64`, `/usr/lib`), svgpp. See
`README.md` (per-distro: Ubuntu/Debian, Fedora/RHEL, macOS, Windows) and `INSTALL.txt` for the package list.

## 4. Conventions on this fork (follow these)

- **Root-cause before editing.** When a test fails or behavior looks wrong, check `git log` for the
  code path first. Two of our three historical test "failures" were *stale goldens* (deliberate
  behavior changes where the test lagged) and only one was a real bug. The fix differs completely
  depending on which it is. Do not blindly regenerate a golden to green.
- **Feature toggles are real preferences.** e.g. gerber improvements are gated on
  `QSettings` `gerberExportImprovementsEnabled`; tests must **pin** such settings to be deterministic.
- **Golden-test the engine logic**, not just smoke tests (gerber bytes, SVG parse geometry).
- **Commit style:** Conventional Commits (`feat(scope):`, `fix(scope):`, `test(scope):`, `docs(scope):`).
  Small, landable, reviewable slices — reviewable diff size is the maintainer bottleneck.
- **Never destroy uncommitted work.** Before any destructive Git command (`git checkout --`, `git restore`,
  `git reset`, `git clean`, patch reversals), commit the current work or save a snapshot/patch to the backup
  location. A path-level `git checkout --` on uncommitted code permanently erases the only copy. Use
  `git clean -fd` only for ignored scratch; do not use `-fdx` unless scratch is backed up.
- **New feature work:** prefer a new `feature/*` branch from `develop`, land as its own slice, and
  open a PR into `develop`. Update the relevant `docs/design/*` and append to `docs/internal/*`.

## 5. Tests — how to build & run (all must stay green)

`tests/auto/auto.pro` builds these suites. Boost suites run headless; QtTest suites need
`-platform offscreen`.

```sh
mkdir build-tests && cd build-tests
qmake6 -o Makefile ../tests/auto/auto.pro boost_root=/usr
make -j"$(nproc)"
# Boost suites:
./test_svg/test_svg
./test_textutils/test_textutils
./test_gerber/test_gerber
./test_svg2gerber/test_svg2gerber
./test_ngspice_simulator/test_ngspice_simulator
./test_project_properties/test_project_properties
./test_breadboard_routing_score/test_breadboard_routing_score
./test_breadboard_route_graph/test_breadboard_route_graph
./test_folderutils/test_folderutils
# QtTest suites (headless):
./test_panelizer/test_panelizer         -platform offscreen
./test_gerberpreview/test_gerberpreview -platform offscreen
./test_peviewlayermap/test_peviewlayermap -platform offscreen
# test_pesvgeditor is now QTEST_MAIN (widgets raster render), so it also takes -platform offscreen.
./test_pesvgeditor/test_pesvgeditor     -platform offscreen
```

- **Boost suites:** "no assertion failures were found" == pass (run bare, no `-platform offscreen`; that arg
  is QtTest-only). Note: `test_pedocument` was **deleted** in Slice E (it tested the now-removed legacy
  `PEElement`/`PEDocument` model); its live `PEPath` round-trip goldens were ported into `test_pesvgeditor`.
- **QtTest suites:** print a `Totals: N passed, 0 failed` line, and all take `-platform offscreen`
  (headless raster). `test_pesvgeditor` is now `QTEST_MAIN` because the self-paint renderer paints to a
  `QImage` via the widget stack — it is no longer GUI-less-only.
- `test_qsysinfo` is **intentionally not wired** into `auto.pro` — its golden hardcodes one specific
  machine (OS/kernel/arch), so it can only ever pass on that host. Do not wire it in; if revived,
  rewrite it to assert behavior, not a machine-specific golden.
- **qmake path-keying hazard:** in test `.pro` files use explicit `$$absolute_path()` entries for ALL
  sources/headers. Mixing `$$files(*.h)` globs (which key rules differently) with absolute entries emits
  *duplicate* moc object rules and breaks the link. Concretely: a relative `$$files(*.h)` self-glob of a
  suite dir *plus* absolute `$$absolute_path(../../../src/…/x.h)` entries for Q_OBJECT headers (e.g.
  `debugdialog.h`) makes qmake emit both a relative- and an absolute-keyed rule for each `moc_<x>.o`, so
  the meta-object TU compiles to an **empty** object only under the aggregate `-j` build — link then fails
  with `undefined reference to main` / missing `TextUtils::*` / `vtable for SvgFileSplitter`. Each suite
  passes in isolation, which hides it. **Fix (now applied to every suite):** drop the `$$files(*.h)`/
  `$$files(*.cpp)` self-globs and list each source with a single `$$absolute_path()` entry (name the
  suite's own `test_*.cpp`(s) explicitly). Verify with a fresh clean `-j` aggregate build, not isolation.
- **QtTest `CONFIG += testcase` requires the test class to live IN the `test_*.cpp` source.** The
  in-source `test_<name>.moc` rule is generated by moc'ing the *test source*; a `Q_OBJECT` that exists
  only in a sibling `test_<name>.h` is **not** scanned, so no `.moc` rule is emitted and the build dies
  with `No rule to make target 'test_<name>.moc'` — in the aggregate build *and* standalone. This is
  **not** a path-keying problem (copying `test_panelizer`'s `.pro` verbatim, renamed to the new suite,
  fails identically), so debugging it through `.pro` paths is a dead end — check where `Q_OBJECT` is
  declared. Declare the test class inside the `.cpp` (as `test_panelizer` does) and list that source
  relatively (`SOURCES += test_<name>.cpp`) so the `.moc` rule keys correctly; keep every *other* source
  absolute per the hazard above. `tests/auto/test_peviewlayermap/` is the worked example.
- **Toolchain drift — check `qmake6 -query QT_VERSION` before trusting "suite green".** AGENTS.md
  recorded Qt 6.11.2 as the tested floor; the box has since moved to **Qt 6.12.0** (all 14 suites are
  green on it). Do not conclude that a Qt bump broke the Boost suites from one failing build: the
  `TextUtils::*` / `vtable for ...` link failures above have been observed with `textutils.o` compiling
  to **0 bytes**, then reproduced fine on the very same toolchain. That empty-object outcome is the
  hazard in the bullet above, not a Qt-version defect, and attributing it to the toolchain sends you
  down the wrong path. A **second** way to get a 0-byte `.o` (seen adding `test_pesvgeditor`): a
  `make` that is killed mid-compile (e.g. a foreground build hitting a tool/exec timeout) leaves a
  truncated 0-byte object that a later `make` treats as up-to-date, so the *next* aggregate build links
  with `undefined reference to main` / missing `TextUtils::*` even though nothing in the sources is
  wrong. Diagnose by `stat -c%s …/textutils.o` (0 bytes vs the real ~780 KB) and, crucially, the
  reliable discipline either way: **always rebuild from scratch in a new directory** — a stale or
  half-written object, or a stale prebuilt binary, keeps reporting PASS (or FAIL) after relinking and
  will mislead you.

## 6. Domain knowledge: copper "ground fill" (mass fill) & the DRC

Subtle; subject of upstream issue #4338 (closed as not-reproducible — see gameplan 8.19). These are DESIGN
LIMITATIONS to be aware of, NOT open defects. Knowing them cold saves a future session hours.

- **Two separate "keepout" settings** (do not conflate them):
  - `DRC::KeepoutSettingName` (`"Keepout"`, default via `DRC::KeepoutDefaultMils`) - read by
    `PCBSketchWidget::getKeepout()`; the *netlist* clearance DRC enforces.
  - `GroundPlaneGenerator::KeepoutSettingName` (`"GPG_Keepout"`, default `KeepoutDefaultMils = 10`) -
    read by `PCBSketchWidget::getKeepoutMils()`; the *clearance the pour keeps* from foreign copper.
- **Seeded ground fill** (`fillType="ground"`) flow: `PCBSketchWidget::groundFill()`
  (`src/sketch/pcbsketchwidget.cpp:1411`) -> `collectGroundFillSeeds` + `ConnectorItem::collectEqualPotential`
  expand each user-picked seed connector to its whole net -> `GroundPlaneGenerator::generateGroundPlaneFn`
  (`src/svg/groundplanegenerator.cpp:257`) rasterizes the whole copper layer, keeps copper inside the
  ground-seed zones, and carves everything else out as the keepout -> result merged onto the
  `groundplane`/`groundplane1` layer as new `GroundPlane` items (`src/items/groundplane.cpp`).
- **CRITICAL (root of #4338):** `GroundPlane` carries **no net**, and `DRC::startAux`
  (`src/autoroute/drc.cpp`) builds its per-net raster from `ViewLayer::copperLayers()` then explicitly
  `.removeOne(GroundPlane0/1)` (drc.cpp:392-393 and 488-489). So the poured copper is **not** part of any
  net and is **excluded from every DRC clearance/short check** -> a pour that bridges two nets is invisible
  to DRC by design. A fix that "makes DRC see the fill" must stop stripping those layers AND give the fill
  a net (or treat it as a foreign-net obstacle in `splitNet`).

## 7. How to pick up this project in a new session


1. `git fetch landracer && git checkout develop && git pull` (canonical line).
2. Skim `docs/panelizer.md`, `docs/internal/panelizer-progress.md`, and `docs/internal/issue-triage-log.md`
   for current feature state and which reported issues we already shot down (so you don't re-debug a closed one).
3. Pick your slice → `git switch -c feature/<thing>`.
4. Implement, add/extend tests under `tests/auto/`, keep the suite green (§5), commit small (§4).
5. PR into `develop`.
