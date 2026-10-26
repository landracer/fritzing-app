# Feature backlog — deferred upstream PRs (decision-grade)

> **Living document.** These are the upstream PRs triaged as **features / large work**, not micro-bugs —
> so we did **not** auto-port them. Each entry gives the owner enough signal to cherry-pick what to land
> and what to leave behind. "Labeled a feature" != "worth landing". Read the **TL;DR** first.

## TL;DR (recommendation per PR)

| PR | title (short) | kind | upstream state | vs develop | my read |
|----|---------------|------|----------------|-----------|---------|
| **#4329** | breadboard **CSV import** w/ column mapping | feature | draft, MERGEABLE | clean | clean + self-contained; top candidate if you want CSV ingest |
| **#4323** | breadboard **autorouter** | feature | draft, CONFLICTING | needs rebase | **ACCEPTED & ported** — quality was high enough that we re-derived it onto the Qt-6 line (see §10) |
| **#4076** | **AC/transient sim** (oscilloscope) | feature | draft, CONFLICTING | rebase + parts PR | large, coupled to a *parts* PR; overlaps our sim fork |
| **#4014** | **CMake** build migration | infra | draft, CONFLICTING | heavy | our qmake line is green — **leave** |
| **#4294** | embed **Blockly** visual programming | feature | CONFLICTING | 17.5k/253k diff | enormous; **leave** |
| **#3966** | simulator fixes (RGB LED, photocell) | fix-set | MERGEABLE (`main`) | stale | port by hand if sim work starts |
| **#3984** | better ngspice-load error msg | tiny fix | MERGEABLE (`main`) | clean | trivial; could port now |
| **#4004** | add NAND gate example | example | MERGEABLE (`main`) | clean | needs companion parts PR |
| **#3973** | hybrid-connector tooltip (#3578) | fix | MERGEABLE (`main`) | clean | small real fix; port-able now |
| **#3928** | swap-timer interval | micro-fix | CONFLICTING | stale | author unsure it's right; **leave** |
| **#3587** | part-bin/svg load speedup | perf | CONFLICTING | stale(2020) | re-derive vs current code |
| **#3401** | **tent vias** | feature | CONFLICTING | stale(2018) | verify still wanted |
| **#3673** | doxygen config/script | docs | MERGEABLE | clean | bulky; **leave** unless wanted |

> Several simulator/feature PRs **base on `main`**, not `develop` — they predate our Qt-6 line and must be
> cherry-picked onto the floating-Qt `develop`, not merged directly.

---

## Per-PR detail

### #4323 — Breadboard auto placement + routing (justin-romano) — **ACCEPTED & PORTED**
- **Scope:** 29 files, +6458/-21. New subsystem under `src/autoroute/breadboard*.cpp` (topology, placement
  kernel, route graph, routing score) + mainwindow/menu seams + 2 new test suites
  (`test_breadboard_route_graph`, `test_breadboard_routing_score`).
- **Was the code good?** **Yes — genuinely clean.** Isolated new files that add a subsystem without rewriting
  existing ones; it touches `mainwindow` only at seams that already exist on develop (`createAutorouteButton`,
  the routing status signal/slot, `setIgnoreSelectionChangeEvents`); it ships its own Boost test suites; it
  reads tuning via `QSettings` with an in-toolbar slider widget. This is the model of how a feature PR to this
  repo should look — additive at the boundary, self-tested, no cross-subsystem churn. It is *why* this one got
  landed when the others were not: the hygiene made a faithful port cheap.
- **How it was ported (so it's not re-litigated):** applied with a 3-way merge; every new file + the
  `pri/autoroute.pri`/mainwindow/auto.pro seams applied cleanly against develop's Qt-6 line. It carried **one
  real latent bug**: both `BreadboardRouteGraph` and `BreadboardRouteGraphCore` declared their ctor with a
  default argument `= Options()` referencing a struct whose *default member initializers* are not complete until
  the class closes — ill-formed C++ (compiles on some compilers, not ours). Fixed by dropping the default arg
  and providing an out-of-line delegating ctor overload that constructs `Options()` after the type is complete.
- **Recommendation:** LANDED on `develop` via `feature/breadboard-autorouter`. Kept for its quality and because
  an isolated, tested breadboard router may genuinely help someone; recorded here so the "is this worth
  re-doing?" question has an answer.

### #4329 — Breadboard wiring CSV import + column mapping (cdrogers, draft, MERGEABLE)
- **Scope:** 28 files, +9846/-2. CSV ingest: preview, delimiter/encoding detection (UTF-8/BOM/UTF-16),
  column auto-map + manual map, DIP 300/600-mil geometry, MPN resolution, placement.
- **Quality:** self-contained, mergeable as-is (no conflicts), ships previews. Big but coherent.
- **Recommendation:** best feature candidate **if** you want CSV-driven breadboard wiring. Decide by want.

### #4076 — AC / transient simulation + oscilloscope (failiz, draft, CONFLICTING) — DITCHED
- **Scope:** 26 files, +954/-143. Transient/AC analysis path (not just DC operating point), drives new
  oscilloscope / waveform-generator parts, animates LED state across sim time.
- **Was the code good or garbage?** *Reasonable intent, but hard to take as-is.* The sim engine changes are
  coherent, but the feature is **coupled to a companion `fritzing-parts` PR** (the scope/generator part
  symbols do not exist in the app alone), it carries a long unresolved TODO list, and it is built on the
  pre-Qt6 `main` base. The coupling is the dealbreaker: the app half is dead weight without the parts half.
- **Why we ditched it (not a merge conflict — a product decision):** our line already carries its own
  simulation fork (`1.0.4_trans_sim_dev`). Landing a *second*, divergent sim engine would give us two
  incompatible simulation models to reconcile forever. That is strictly worse than not having AC sim.
- **What the original author should consider** to make this landable: (1) rebase onto the Qt-6 `develop`;
  (2) land the `fritzing-parts` scope/generator parts **first** as their own merged change so the app change
  has something to bind to; (3) reconcile against (or supersede) the existing `trans_sim` engine rather than
  shipping a parallel one; (4) close or drop the open TODOs. Re-open as a fresh PR against `develop` only once
  (1)–(3) are settled.
- **Reflection — *Why?*:** *Why a second sim engine when we have one?* If the real goal is "AC and transient
  analysis," the cheapest win is teaching our existing simulator those analyses — not bolting on a rival.
  Answer that and the diff shrinks to the analysis math plus a scope widget, which is landable.

### #4014 — CMake build (feliwir, draft, CONFLICTING)
- qmake->CMake (git versioning, parts-db target). CI + CPack + qmake removal still TODO. Our qmake line is our
  whole build contract and builds green. Whole-repo churn for an unfinished migration. LEAVE.

### #4294 — Embed Blockly@rduino visual programming (SebCanet, OPEN, CONFLICTING)
- +17578/-253354 (14.9 MB diff). Embeds a JS visual-programming tool + i18n + board sync. Enormous. LEAVE
  unless the editor direction is a committed product goal.

### #3966 / #3984 / #4004 / #3973 — failiz simulator & misc (all base on `main`)
- **#3984** ngspice-load error msg (+7) and **#3973** hybrid-connector tooltip (#3578, +28/-10) are small,
  self-contained, MERGEABLE — cleanly port-able now.
- **#3966** (RGB LED/photocell/scale smoke) real sim improvement but 2022-era, `main` base.
- **#4004** NAND example + needs companion MOS-NAND parts PR.
- Recommendation: #3984/#3973 = low-risk micro-fixes hiding among features — say the word, I port onto develop.
  #3966/#4004 tie to the sim fork — defer to a sim decision.

### #3928 — swap timer interval 30->300 (failiz, CONFLICTING)
- Author says "not sure this is the right way of fixing this." Stale. LEAVE.

### #3587 — part-bin / SVG load speedup (DrItanium, CONFLICTING, 2020) — DITCHED
- **Scope:** avoids rebuilding a `QDomDocument` per part when loading the part bin / sketches.
- **Was the code good or garbage?** *Good idea, stale body.* The optimization (parse once / cache rather than
  re-parse per module) is a real win, but the patch is ~6 years old and keys off code we have since rewritten.
  It is not garbage — it is just no longer applicable line-for-line.
- **Why we ditched it:** it was auto-graded `stale`; porting it line-by-line would drag 2020-era context and the
  old `PaletteModel`/`FApplication` shape into our restructured tree. A clean re-derivation against today's
  `TextUtils`/`ModuleInfoParser`/`Palette*Model` is cheaper and safer than resolving six-year-old conflicts.
- **What the author should consider:** re-measure first. Is `loadModuleRefs` still O(n) QDomDocument parses on
  current `develop`? If a profile still shows it hot, open a *new* small PR that caches the parsed module set
  (or moves to the shared `QDomDocument` load path) against current code, with a micro-benchmark showing the win.
- **Reflection — *Why?*:** *Why carry this at all if it was never profiled on the modern tree?* A perf PR with no
  before/after measurement on current code is a hypothesis, not a fix. Bring a number and it becomes landable.

### #3401 — Tent vias (Intrinsically-Sublime, CONFLICTING, 2018) — DITCHED
- **Scope:** tiny (+23/-2) — adds a "tenting" mask option for vias (cover a via with solder mask).
- **Was the code good or garbage?** *Small and harmless, but unverified and ancient.* Nothing wrong with the
  size; the problem is it is an 8-year-old change wired into a mask/silkscreen layer path that predates our
  current `ViewLayer`/mask handling, with no test and no confirmation the feature is even still wanted.
- **Why we ditched it:** `stale`. Re-deriving against current `ViewLayer`/mask code is trivial given the size;
  porting the old hunks is not obviously still correct.
- **What the author should consider:** confirm tenting is still a desired capability, then re-implement on current
  `ViewLayer` mask handling in a fresh, small PR (ideally with a gerber-export golden test proving the tented via
  renders in the mask layer).
- **Reflection — *Why?*:** *Why ship a mask feature with no way to verify it?* A one-line golden test on the
  exported mask gerber answers both "does it work" and "is it still wanted" — that's the missing piece here.

### #3673 — Doxygen config + script (mMerlin, MERGEABLE)
- 2563-line Doxyfile + script generating ~940 MB HTML, linux-only. Harmless but bulky. LEAVE unless wanted.

## How to use this doc
1. Mark each row **PORT** or **LEAVE** (edit the TL;DR table or tell me).
2. I port the PORTs onto fresh topic branches (clean, per-subsystem), keep the suite green, open one PR.
3. Anything you LEAVE stays recorded here so it's never re-raised as "did we forget this?".
