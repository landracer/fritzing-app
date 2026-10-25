# Feature backlog — deferred upstream PRs (decision-grade)

> **Living document.** These are the upstream PRs triaged as **features / large work**, not micro-bugs —
> so we did **not** auto-port them. Each entry gives the owner enough signal to cherry-pick what to land
> and what to leave behind. "Labeled a feature" != "worth landing". Read the **TL;DR** first.

## TL;DR (recommendation per PR)

| PR | title (short) | kind | upstream state | vs develop | my read |
|----|---------------|------|----------------|-----------|---------|
| **#4329** | breadboard **CSV import** w/ column mapping | feature | draft, MERGEABLE | clean | clean + self-contained; top candidate if you want CSV ingest |
| **#4323** | breadboard **autorouter** | feature | draft, CONFLICTING | needs rebase | *well-built*, but you said not on-board — default **leave** |
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

### #4323 — Breadboard auto placement + routing (justin-romano, draft, CONFLICTING)
- **Scope:** 29 files, +6458/-21. New subsystem under `src/autoroute/breadboard*.cpp` (topology, placement
  kernel, route graph, routing score) + mainwindow/menu seams + 2 new test suites.
- **Quality:** *actually clean* — isolated new files, only touches mainwindow at seams, ships its own tests.
  NOT a mess. The decline reason is **product direction** (you don't want a breadboard autorouter), not hygiene.
- **If ported:** needs rebase onto our Qt-6 develop (CONFLICTING, predates our restructure); ongoing maint. surface.
- **Recommendation:** LEAVE (your call; recorded so it isn't re-litigated).

### #4329 — Breadboard wiring CSV import + column mapping (cdrogers, draft, MERGEABLE)
- **Scope:** 28 files, +9846/-2. CSV ingest: preview, delimiter/encoding detection (UTF-8/BOM/UTF-16),
  column auto-map + manual map, DIP 300/600-mil geometry, MPN resolution, placement.
- **Quality:** self-contained, mergeable as-is (no conflicts), ships previews. Big but coherent.
- **Recommendation:** best feature candidate **if** you want CSV-driven breadboard wiring. Decide by want.

### #4076 — AC / transient simulation + oscilloscope (failiz, draft, CONFLICTING)
- 26 files, +954/-143. Transient analysis vs operating-point; drives new scope/wave-generator parts; animates
  LED state over sim time. **Coupled** to a fritzing-parts PR (scope/generator parts don't exist without it).
  Long open TODO list; pre-Qt6 base. Overlaps our `1.0.4_trans_sim_dev` sim line — reconcile first. LEAVE.

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

### #3587 — part-bin / SVG load speedup (DrItanium, CONFLICTING, 2020)
- Real perf win (avoid rebuilding QDomDocument per part) but 6 yrs old; re-derive vs current TextUtils/
  PaletteModel before use. LEAVE unless perf is a goal.

### #3401 — Tent vias (Intrinsically-Sublime, CONFLICTING, 2018)
- Tiny (+23/-2) but from 2018; verify mask/silkscreen path still matches. LEAVE.

### #3673 — Doxygen config + script (mMerlin, MERGEABLE)
- 2563-line Doxyfile + script generating ~940 MB HTML, linux-only. Harmless but bulky. LEAVE unless wanted.

## How to use this doc
1. Mark each row **PORT** or **LEAVE** (edit the TL;DR table or tell me).
2. I port the PORTs onto fresh topic branches (clean, per-subsystem), keep the suite green, open one PR.
3. Anything you LEAVE stays recorded here so it's never re-raised as "did we forget this?".
