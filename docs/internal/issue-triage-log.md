# Issue triage log

> **Living document.** Newest-first, dated. This is the permanent record of *upstream issues we
> investigated and shot down* — the "did we actually have a bug here, and what did we find" log that
> complements the feature-development logs (`panelizer-progress.md`). Read the top to know current state.

## Conventions

- **Newest-first.** Newest dated entry at the top (after this conventions block).
- One entry per triaged issue: **issue # → what we suspected → what we found → disposition → commits.**
- **Dispositions:** `FIXED` (real bug, code changed) · `NOT REPRODUCIBLE` (could not reproduce) ·
  `USER ERROR` (report reflects misuse, not a defect) · `WONTFIX` · `DESIGN LIMITATION` (real but by
  design, not a bug).
- Link the mechanism/anchor file+lines so a future reopen starts warm, not cold.

---

## 2026-10-25 — Feature backlog decisions: 1 ported (#4323), rest formally ditched
**Disposition:** of the deferred *feature* PRs, **only #4323 (breadboard autorouter) was landed**; the rest are
**ditched**, each with an author-facing write-up in `docs/internal/feature-backlog.md` explaining why. This
turn also overhauled `README.md` / `INSTALL.txt` (per-distro install incl. Ubuntu) and `AGENTS.md`.

- **#4323 breadboard autorouter — LANDED** on `feature/breadboard-autorouter`. Ported with a 3-way apply; all
  new `src/autoroute/breadboard*.cpp` + the `pri/autoroute.pri`/mainwindow/auto.pro seams applied cleanly against
  the Qt-6 `develop`. Its latent DMI default-argument bug (ctor `= Options()` using not-yet-complete default
  member initializers) was fixed with an out-of-line delegating ctor overload. All 8 Boost suites + 2 QtTest
  suites green on Qt 6.11.2.
- **#4076 AC/transient sim, #3587 part-load perf, #3401 tent vias — DITCHED.** Auto-graded `stale` and
  (for #4076) coupled to a parts PR + overlapping our own sim fork. Per the repo rule "messy PRs targeting
  restructured subsystems are re-derived, not ported," these stay backlog. Each entry now carries: scope →
  was-the-code-good-or-garbage → why-ditched → what-the-author-should-consider → a *Why?* reflection.
- **Why #4323 got an exception to "re-derive, don't port":** its hygiene (isolated additive files, touches only
  existing seams, self-tested) made a faithful port cheap and low-risk — the exception that proves the rule.

---

## 2026-10-25 — Upstream PR backlog triage (all 36 open `fritzing/fritzing-app` PRs)
**Disposition:** bulk triage of the open-PR backlog → **5 ported as real fixes**, **rest = LANDED / OBSOLETE / FEATURE-BACKLOG**. See matrix below; commits for the ported fixes noted per-row.

**Method (port-first, content-level):** for each open PR we fetched its diff and asked *"is this
change already carried on `develop`, and if not is it a genuine behavior fix?"* — not branch
ancestry (most are squash-merged so git can't see equivalence). 5 PRs are genuine, isolated,
behavior-correcting fixes **not** on develop → ported in commit `c53dcf74` on `fix/upstream-micro-fixes`.
Everything else is either already carried (superseded by our Qt-6/panelizer line), obsolete (stale
base), or a *feature* that is not an issue to fix but a "added feature" to be chosen deliberately.

### PORTED (genuine fixes, commit `c53dcf74`, all build+link on Qt 6.11.2)
- **#4341** negative zoom — `fitInWindow()`/`updateZoomFromCurrentTransform()` now `qAbs(transform().m11())`
  so a mirrored/rotated view reports a positive zoom %. (develop had raw `m11()` → negative.)
- **#3755** voltage fallback — `symbolpaletteitem` had a dead `else { if (ok) }` so `DefaultVoltage`
  never fired; fixed (set default in the `else`, hoist `setLocalProp` out of the branch). Real bug.
- **#4311** QStringBuilder — `folderutils` ternary mixed `const char*` with `QString` (type mismatch);
  guard both arms as `QString(...)`. (develop had the unfixed `? "" : ... + join(...)`.)
- **#4336** reproducible `parts.db` — `insertPart()` strips the random per-session PartFactory temp folder
  from the stored path (`QFileInfo(path).fileName()`). (develop lacked it.)
- **#4033** C++17 cleanup — drop deprecated `register` storage class in `clipablewire` `CrossingsTest`.

### ALREADY ON DEVELOP / SUPERSEDED (close as LANDED — equal-or-better fix already carried)
- **#3757** checkMoved readability — develop *rewrote* `checkMoved` (id-keyed `QHash` + validation loop);
  the PR targets a superseded older structure. OBSOLETE.
- **#3896** jump-label right-click — develop already has the `Qt::RightButton` guard (partlabel.cpp:193)
  and `m_doDrag = false` in mouseReleaseEvent (254). LANDED.
- **#4342** quazipdetect stray text (`intuisphere`) — develop's quazipdetect rewrite removed it. LANDED.
- **#3993** quazip pkg-config — superseded by develop's major-version-keyed quazip scheme. SUPERSEDED.
- **#3864** libgit2 `LIBGIT_STATIC` codename logic — develop already on floating/dynamic libgit2;
  bionic/focal codename branching is stale. OBSOLETE.
- **#3754** ruler rounding — develop's `0.196875` constant is numerically equivalent to `.125 + (1.15/16)`.
  NO-OP cleanup. OBSOLETE.

### BUILD/DOCS (stale, do not port)
- **#3831** hicolor icon path (develop uses its own install layout), **#3822** libgit2 doc,
  **#3770** mingw (huge/stale), **#4331** MSVC libs (targets the same quazipdetect we rewrote → conflict),
  **#4302** #4268 Qt5→Qt6 (we already did Qt6).

### FEATURE / TRANSLATION BACKLOG (not "issues to fix" — deliberate feature decisions)
- **Features:** #4323 breadboard autorouter (draft), #4329 breadboard CSV import (draft),
  #4294 embed visual programming tool (14.9 MB diff), #4076 AC simulation (draft), #4014 CMake build (draft),
  #3401 tented vias, #3587 part-load speedup, #3673 doxygen, #3966/#3984/#4004/#3973 simulator bits,
  #3928 swapTimer interval.
- **Translations/licensing:** #4289 Russian (conflicting), #4237 trans+sim, #4250 licenses+README,
  #4313 macOS icns (binary), #3822 libgit2 docs.

**Non-regression proof:** the 2 non-green suites (`test_gerber`, `test_ngspice_simulator`) SIGABRT on
*clean develop too* — `test_ngspice` configure even logs the real cause: *"ngspice not found in
/tmp/ngspice-42"* (hardcoded harness path; real install is `~/Documents/git/ngspice-42`), and `test_gerber`
aborts in the offscreen golden harness. Both are pre-existing env/harness fails; none of the 5 touched
files feeds those subsystems. The 6 suites exercising touched code pass: test_svg, test_textutils,
test_svg2gerber, test_project_properties, test_panelizer (6/6), test_gerberpreview (8/8).

**Commits:** `c53dcf74` (the 5 ported fixes) on `fix/upstream-micro-fixes`.

---

## 2026-10-24 — #4338: "ground plane pour bridges foreign nets / DRC ignores pour geometry"
**Disposition:** NOT REPRODUCIBLE / likely USER ERROR (a real-but-intended design limitation, *not* a bug)

**What was suspected (the mechanism traced):** the reporter's board, once a ground fill is applied,
supposedly pours copper over a *foreign* net, shorting two nets that should stay separate, and the DRC
stays silent because it ignores the fill geometry.

**Root-cause analysis (real mechanism — this part is accurate and useful):**
- `PCBSketchWidget::groundFill()` (`src/sketch/pcbsketchwidget.cpp:1411`): `collectGroundFillSeeds()` →
  `ConnectorItem::collectEqualPotential()` expands each user-picked seed to its whole net; the *ground*
  net's copper is rendered; the fill polygon = board **minus** all other copper (the keepout), merged onto
  the board's `groundplane`/`groundplane1` layer as new `GroundPlane` items.
- `GroundPlaneGenerator::generateGroundPlaneFn()` (`src/svg/groundplanegenerator.cpp:257`): keeps copper
  inside the ground-seed zones; everything else is carved away (keepout) via `ctDifference` with
  `copperWithoutGroundConnectors`; foreign copper shrunk by `keepoutMils` both sides. Default
  `GPG_Keepout` = 10 mil (`GroundPlaneGenerator::KeepoutDefaultMils`).
- **The "DRC ignores the fill" fact is true and by design:** `DRC::startAux()` (`src/autoroute/drc.cpp`)
  builds its per-net raster from `ViewLayer::copperLayers(placement)` then
  `.removeOne(GroundPlane0); .removeOne(GroundPlane1);` (`drc.cpp:392-393` **and** `488-489`). A
  `GroundPlane` carries **no net**, so it is never part of any net's `equi`/raster — a fill is *not* a
  checked conductor and *is* excluded from clearance checks. So a netless pour cannot itself be flagged.

**Live reproduction attempt (the owner, human):** ran the full sequence — Routing → *Choose Ground Fill
Seed(s)...* → *Ground Fill* → *Design Rules Check (DRC)* (Shift+Ctrl+D) — on **both** the reporter's board
(minus its one genuinely-missing part) **and** the owner's own board, across multiple fill/keepout
variations (`Set Ground Fill Keepout...`). Verbatim result: *"I can only get it to fail DRC with legit
size mismatches ... non issues as I see it working as expected."* In every variation the fill correctly
avoided foreign copper; **no** fill ever bridged a foreign net that wasn't already a *real,
already-flagged* trace-trace clearance problem. The reporter's attached `.fzz` in fact stores
`GPG_Keepout`/`DRC_Keepout` at the 10-mil defaults and no user `fillType="ground"` fill — i.e. the board
as attached exercises neither the fill nor a fill-bridging path.

**Conclusion:** for a normal, competently-routed board the ground-fill carve + DRC behave **as designed**.
A netless pour only "misleads" in the sense that *its own* copper isn't a net — which is correct (the fill
is a single net's copper; it can't be "foreign"). The reporter's symptom is consistent with a **user**
misreading a normal clearance flag, or attaching a board that has no actual ground fill. No speculative
fix was written against an unreproducible report.

**Reopen condition:** a future report must ship a board that **actually** stores `fillType="ground"` *and*
visibly bridges two distinct nets in the GUI; then the §8.18 mechanism above is the head start (the only
code changes that would matter are: stop stripping `GroundPlane0/1` in `drc.cpp`, and/or bind the fill to
its net).

**Commits:** `25e91d8b` (root-cause capture), `45b950bd` (owner live-test outcome + reframe).
