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
- **Welcome landing page (newest):** the app now opens on a Welcome tab (`src/sketch/welcomeview.*`)
  — recents list on the left, a live SVG-rendered **board preview** + gerber-production status on the
  right, and recents housekeeping (Remove / Purge-all). Opening a file loads into the *current* blank
  window (second file → new window). Full arc + learnings: `docs/internal/panelizer-progress.md`,
  Session 9.
- **Tests:** all wired test suites build and pass on Qt 6.11.2 (see §5). `test_folderutils` was wired
  into `auto.pro` and the Boost-suite `.pro` files were converted to all-explicit `$$absolute_path()`
  source/headers (the path-keying hazard that made the aggregate `-j` build fail nondeterministically).

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
  - `docs/design/vector-editor.md` — spec for turning the parts editor into a real in-app vector
    editor (port of svgedit's `core/` model into the Qt `pe*` editor; phased P0–P5).
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
```

- **Boost suites:** "no assertion failures were found" == pass.
- **QtTest suites:** print a `Totals: N passed, 0 failed` line.
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
