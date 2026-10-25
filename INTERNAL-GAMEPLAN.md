# Fritzing Modernization — Internal Game Plan

> **Status:** DRAFT v1 — internal working reference only.
> **Do not stage, commit, or push this file.** It is our shared point of truth for
> driving the repo to A-class. It lives at the repo root and stays in the `??` untracked
> set like the other `homerun*.md` / `pr-*.md` scratch files until we promote pieces
> into real `docs/`.

> **Decision already made:** this is a **fork-of-record**. `landracer/fritzing-app`
> becomes the standalone canonical project. Upstream `fritzing/fritzing-app` is
> demoted to a read-only remote we cherry-pick from, not a master we await.

---

## 0. Why this exists (the honest diagnosis)

We inherited a **well-seasoned but moribund** EDA app: real, mature C++/Qt CAD logic
(~160k LOC across 548 source files, 3,495 closed issues of accumulated domain knowledge)
trapped in a repo that publishes ~2 commits / 90 days, whose last release tag is from
2020, and whose README still points at dead Travis badges.

**The single most important strategic finding, verified not assumed:**

> The app is **not** logically "years behind Qt." It is pinned to Qt **≤ 6.5.10** by a
> self-imposed guard — `QT_MOST=6.5.10` in `phoenix.pro:22` — plus exactly **10 lines**
> of `QString::arg()`/`QChar()` call-sites that stop compiling on Qt 6.9+.
>
> Proof (verified on this machine, system Qt = **6.11.2**):
> - `QString().arg(Qt::Orientations)` → **compile error** (`no matching function for call to 'QString::arg(Qt::Orientations&)'`)
> - `QString().arg(static_cast<int>(Qt::Orientations))` → **compiles clean**
>
> See `git show 80507188` (the forward-compat fix). The entire Qt gap is: raise the cap +
> land those casts. Nothing about the CAD core needs re-platforming.

**Implication:** "get the repo current" is *not* a rewrite. It is: promote a fork that is
already 2151 commits current, raise a version guard, land a 10-line diff, then systematically
raise quality. The barrier was never technical — it was a closed PR channel and a maintainer
whose bottleneck is *reviewable diff size*, not code generation (their own words on #4314).

**Our whole thesis:** the moat is no longer "does the code work." Any agent can churn out
working C++. The moat is *provable, reviewable, well-documented, well-tested* change delivered
in landable slices. Everything below optimizes for that.

---

## 1. Grading rubric (how we score ourselves, and where we are)

We grade the repo A–F on five axes. "A-class" = every axis at A. Current composite ≈ **C+**.

| # | Axis | Grade now | What "A" looks like |
|---|------|-----------|---------------------|
| 1 | **Build & toolchain currency** | **D** | Builds green on a blessed modern Qt (≥6.8) out of the box; cap reflects reality; deps vendored to known-good versions; clean configure on a fresh clone. |
| 2 | **Upstream velocity / issue health** | **C** | Open-issue backlog triaged and shrunk, not 463-deep-and-growing; every open item has an owner + a plan or a "wontfix with reason". |
| 3 | **Feature completeness (panelizer / gerber preview)** | **B** | Both features finished, edge-cases closed, documented, and the blend-mode heterogeneous-rendering TODO resolved; tests are real assertions not smoke tests. |
| 4 | **Test rigor** | **C+** | Real golden-value assertions on engine logic (layout geometry, gerber parse geometry) — currently honest smoke tests. Coverage of the 548-file core, not just the 6 new commits. |
| 5 | **Repo hygiene** | **D** | No scratch docs in tree, no build artifacts committed, no stale 2023 files, `docs/` is the living source of truth, a real `AGENTS.md` exists. |

**Composite: C+.** The two D's (toolchain currency, hygiene) are the cheapest to fix and
unlock everything else. The B on features is an asset to polish, not rebuild.

---

## 2. The fork-of-record migration (P0 — first thing we do)

Upstream `main` is a dead 2020 branch (0 commits ahead of develop, 2151 behind). Do **not**
rebase onto `main`. Base the canonical line on upstream **`develop` tip (`04e5bb02`)** —
that is where all real work lives. Your feature work is already only **6 commits / ~19k
insertions** on top of exactly that tip.

### 2.1 Branch topology to adopt

```
landracer/develop   ← canonical line, seeded from fritzing:develop (04e5bb02) + our 6 commits
landracer/master    ← retarget the fork default OFF this (it tracks dead upstream main)
origin  = https://github.com/fritzing/fritzing-app   (read-only; cherry-pick source)
landracer = https://github.com/landracer/fritzing-app (canonical; we drive it)
```

Steps (execute in order; verify each before the next):
1. Set fork default branch to the develop-seeded line, not `master`.
2. Flatten the 6-commit feature history into a **coherent stack** on the canonical line:
   - `pr/qt6-forward-compat`  (7 files, +10/−10) — the Qt 6.9+ casts. Trivial, land first.
   - `pr/gerber-preview`       (26 files, +4054)  — standalone RS-274X/Excellon viewer.
   - `pr/panelizer`            (80 files, +13877) — wizard + engine; **depends on** gerber-preview.
   - `pr/connector-debug-pref` — **candidate to DROP**: maintainer said the 1.0.8 connector
     refactor supersedes it. Confirm, then close it to bank goodwill.
3. Add upstream as `origin`; keep `landracer` as the push target.

### 2.2 The dirty tree must be resolved before anything else

`git status` shows **21 modified tracked files, uncommitted.** Verified specifics:
- The working tree currently has `static const QRegularExpression` count = **0** in
  `src/gerberpreview/gerberparser.cpp`, but `HEAD` (commit `d01b2d52`, "hoist hot-path
  regexes to optimized statics") has them. **The committed history is missing the perf work;
  it lives only as uncommitted changes.** Real repro hazard: a fresh clone of our own branch
  would not reproduce the build we have locally.
- The uncommitted diff also re-adds the Qt6 `static_cast<int>` casts (forward-compat edits
  sitting unstaged across `commands.cpp`, `textutils.cpp`, etc.).

**Action:** commit the working-tree changes into the correct feature branches so
`landracer/develop` is reproducible from a clean clone. Scratch/agent context must never be
the only copy of behavior-bearing code.

### 2.3 Dependency pins are part of "currency" (not just Qt)

The `.pri` chain pins hard-coded sibling paths — part of the toolchain-currency grade; move to
a single documented source of truth:
- `pri/quazipdetect.pri:14` — **the bug that broke our own configure today.** It builds
  `…/quazip-<QT_VERSION>-1.4)intuisphere` with a **misplaced paren** (the `)` closes
  `absolute_path()` before `intuisphere`), so with system Qt 6.11.2 it seeks a nonexistent
  `quazip-6.11.2-…` dir and hard-errors. Fix the paren **and** decide the blessed Qt.
- `pri/libgit2detect.pri:21` `LIBGIT_VERSION=1.7.1`, `pri/boostdetect.pri` (../boost_1_xx),
  ngspice path, clipper — all resolved via `absolute_path(../…)`. Centralize these.

---

## 3. Repo hygiene (P0 — same wave; cheap, unlocks everything)

Hygiene is a **D** because the tree can't be cleanly reproduced. Concretely:

### 3.1 Generated + binary artifacts are untracked-but-present
`git status` currently shows as `??`: compiled test binaries (`tests/auto/test_*/test_*`),
Qt `moc_*.cpp`, `moc_predefs.h`, `*.moc`, `target_wrapper.sh`. `.gitignore` (verified) has
**no** `moc_*` / `qrc_*` / `ui_*` / test-binary patterns, so they clutter every diff.

Add to `.gitignore`:
```
# Qt build-generated
moc_*.cpp
moc_*.h
*.moc
qrc_*.cpp
ui_*.h
# qtest binaries + wrappers
tests/auto/*/moc_predefs.h
tests/auto/*/target_wrapper.sh
tests/auto/test_*/test_[a-z]*   # the compiled binaries, not the .cpp
```
Do not `git add` any `moc_`/binary. Delete them from disk; regenerating proves reproducibility.

### 3.2 Scratch docs must stop being the only copy of anything
`homerun*.md` (5 files), `panelizer-assesment.md`, `pr-*.md` are all untracked. Fine for
*narrative* history, but **behavior-bearing facts** must not live only there. Rule:
- Narrative / decision-log → stays in untracked scratch files (fine).
- Anything that changes how the code behaves → belongs in tracked `docs/` or code comments.
- **A real, tracked `AGENTS.md` must be created** (none today). It points contributors/agents at:
  the blessed Qt, build command (`tools/build-with-qt.sh`), test command, the "small landable
  diffs" rule, and this doc's promoted `docs/` successors.

### 3.3 Stale content
- `README.md`: dead Travis badges, "maintained by Kjell" framing. Re-author for the fork line.
- `INSTALL.txt`: still says Qt5 → literally upstream issue **#4268**. Fix it.
- `phoenix.pro.user` (2023 Qt Creator state) is gitignored but on disk — leave ignored, don't ship.

---

## 4. Feature polish: panelizer + gerber preview (the crown jewels)

These two are our differentiator — upstream has no answer for them. Polish, don't rebuild.

### 4.1 Known gaps to close (tracked in `docs/panelizer.md`)
- **Blend mode renders every instance at the primary footprint.** Documented TODO. Resolve
  heterogeneous on-canvas rendering so blended boards draw at their true measured size.
- **CLI parity.** Maintainer explicitly asked: "Can the panelizer be triggered in CLI? I'd
  expect the same output for CLI as well as interactive mode." Currently GUI-only. A
  `Fritzing --panelize <fzz> <paneldef.xml>` headless path that calls `PanelizerEngine`
  directly is both a requested feature and the cleanest testable seam (no widget).
- **`Panelizer::makeSVGs()` is kept byte-for-byte** so Gerber output is unchanged — keep that
  invariant guarded by golden-file tests (see §5).

### 4.2 Gerber preview
- Keep it **opt-in beta** (preferences checkbox → runtime toggle, hidden menu item). Match the
  maintainer's stated preference: distinct item under File → Export → Production, hidden unless
  enabled, **no auto-open on export**.
- Treat **gerbv as an oracle**, not a dependency: cross-check our parse/render output against
  gerbv as a second opinion in tests. (Maintainer's own suggestion; defuses "why reinvent a
  gerber viewer".)
- Preserve the aperture-select fix for modern bare `Dnn` form; that's a real correctness win.

### 4.3 Architecture invariant to preserve
Panelizer → depends on → Gerber preview, **never the reverse**. Keep `src/gerberpreview/`
independent of `src/autoroute/panelizer*`. This is why preview lands before panelizer in the
stack.

---

## 5. Test rigor (the real moat — go from smoke to golden)

Current: 13 test files / 10 suites, mostly honest smoke tests (our own PR reply admits this).
Upstream's ceiling is *reviewable* change; our answer must be *provable* change.

### 5.1 Wire the suites that exist but don't run
`tests/auto/auto.pro` `SUBDIRS` currently lists only 7 of 10 suites:
```
SUBDIRS = test_gerber test_svg test_textutils test_svg2gerber test_ngspice_simulator test_project_properties
```
`test_panelizer` and `test_gerberpreview` exist on disk but are **not in SUBDIRS** → they never
run. Fix `auto.pro` first, or the feature we ship is untested in CI.

### 5.2 Upgrade engine tests to golden-value assertions
- **Panelizer layout:** deterministic fixtures → assert exact placed rectangles (x,y,w,h,rot)
  from `GuillotineBinPack` in integer mils. Golden file per fixture. Cover the non-rotatable
  board that must *fail* layout rather than silently rotate.
- **Gerber parse:** golden geometry per RS-274X/Excellon fixture (apertures, coordinates,
  polarity). Cross-check against gerbv output as oracle (§4.2).
- **Round-trip:** emit panel → parse our own Gerber → assert geometry matches the layout model.

### 5.3 CI
No `.github/workflows` exists (verified) — upstream still references Travis. Add GitHub Actions:
headless (`-platform offscreen`) build on a matrix of blessed Qt + `make check` on all suites.
This makes "builds green on modern Qt" an enforced gate, not a claim.

---

## 6. Issue backlog triage (C → A on "issue health")

Reality: 463 open issues + 36 open PRs; only ~183 PRs merged all-time; 422 of the open issues
predate 2025. We can't just "fix all 463." The A-class move is *triage discipline*, not closure
volume.

### 6.1 Triage pipeline
1. **Label hygiene first.** Many are unlabeled (80 issues, no label). Force every open issue to
   exactly one type (bug/enhancement/question) + optional area (UI/PCB-View/Parts/ImportExport).
2. **Triage decision for each:** `needs-repro` | `confirmed` | `wontfix-reasoned` | `good-first`.
   An issue is not "triaged" until it has an owner-facing state.
3. **Wontfix with reason** is a valid, valuable outcome (maintainer hinted some features have
   low demand). 463 open is fine if all 463 are *dispositioned*; 463 open and untriaged is what
   reads as abandonment.
4. Export this triage into `docs/`-linked tracking (templates exist: Bug.md, Enhancement.md).

### 6.2 Fast wins already scoped
- **#4268** INSTALL.txt still references Qt5 → trivial doc fix, ships with §3.3.
- **#4333 / PR #4341** negative-zoom-on-PCB-load → already a small open PR; land on our line.
- **#4326** header "extra" hole-size compliance → real EE correctness bug, good flagship fix.
- **#4342** quazipdetect.pri stray text → same family as §2.3; fold in.

### 6.3 Attracting new contributors
The point of all this is to make the repo *joinable*: green CI, a real README, `AGENTS.md`,
labeled good-first-issues, and this methodology visible. A dead repo becomes live the moment it
has a green build, a test harness, and a documented way to contribute a small change.

---

## 7. Phased execution plan (work against this)

Ordered so each phase is independently landable and verifiable. Do not start Phase 1 before
Phase 0 is committed and reproducible from a clean clone.

### Phase 0 — Stop the bleeding (hygiene + reproducible build)  [target: D→B on axes 1,5]
- [ ] Commit the 21 uncommitted tracked files into correct feature branches (esp. restore the
      `static const QRegularExpression` perf work into `gerberparser.cpp` history).
- [ ] Fix `pri/quazipdetect.pri` misplaced-paren bug (gates any Qt>6.5.10 configure).
- [ ] Add `.gitignore` rules (§3.1); delete generated artifacts from disk; prove regen.
- [ ] Retarget fork default branch to develop-seeded line (§2.1); add upstream as `origin`.
- [ ] Wire `test_panelizer` + `test_gerberpreview` into `tests/auto/auto.pro` (§5.1).

### Phase 1 — Qt currency (kill the D on toolchain)  [target: axis 1 → A]
- [ ] Land the Qt 6.9+ forward-compat casts (`pr/qt6-forward-compat`, +10/−10) on canonical line.
- [ ] Raise `QT_MOST` to the blessed Qt (decide: pin a specific ≥6.8, or move to floating Qt6).
- [ ] Centralize all dep pins (libgit2/boost/quazip/ngspice/clipper) into one documented file.
- [ ] Add GitHub Actions CI: matrix build green on blessed Qt (axis 1 becomes enforced).

### Phase 2 — Feature polish (B→A on features)
- [ ] Resolve blend-mode heterogeneous rendering (§4.1).
- [ ] Add CLI panelize path with parity to interactive (§4.1).
- [ ] Golden-value tests for layout + gerber parse; gerbv oracle cross-check (§5.2).

### Phase 3 — Community & backlog (C→A on issue health, and the real goal)
- [ ] Re-author README/INSTALL for the fork; write tracked `AGENTS.md`.
- [ ] Run the §6 triage pipeline across all open issues.
- [ ] Publish contribution guide; seed good-first-issues.

### Definition of Done (per phase)
A phase is "done" only when: clean-clone build is green, all suites pass in CI, and the behavior
is described in tracked `docs/`. Scratch files are never the only source of a behavior.

---

## 8. DECIDED (owner sign-off — 2026-10-24)
1. **Qt = FLOAT.** We are on a rolling distro and track the newest Qt closely. So the model
   changes from "pin ≤6.5.10" to: **keep a floor (`QT_LEAST`), drop the hard ceiling**, and keep
   the 10 forward-compat `static_cast<int>` sites merged so newer Qt compiles. The floating
   source breaks are ONLY those `QString::arg()`/`QChar()` enum-int sites (verified on 6.11.2).
2. **Connector debug highlight = KEEP / wrap in.** The gated preference (red-canvas connector
   error highlight, off by default) is retained. Supersession concern is overridden by owner:
   it's a useful diagnostic and already gated behind a Beta-Features preference.
3. **Brand = KEEP "Fritzing".** The name carries the EE community and SEO. Signal "community
   fork, now the active line" clearly in the README; let the repo's own shine force upstream.

### 8.1 quazip anti-pattern (root cause, verified)
`pri/quazipdetect.pri:14` builds the include/lib path as
`quazip-<full Qt patch version>-1.4intuisphere`, i.e. it hard-codes a Qt *patch* version into a
filesystem dir name. The installed lib is at `quazip-6.11.1-1.4intuisphere` (lib is major-version
`libquazip1-qt6.so`). On any Qt whose patch ≠ 6.11.1 the path won't resolve → configure dies.
This is the concrete blocker for FLOATING, independent of the misplaced-paren typo.
**Fix direction:** decouple the quazip path from `$QT_VERSION` — key it on major only, or make it
a single overridable `QUAZIP_PATH` variable that doesn't drift with patch bumps.

### 8.2 Sequencing to actually FLOAT (hard ordering — do NOT skip)
Lifting QT_MOST is necessary but NOT sufficient. Verified on Qt 6.11.2:
1. `pr/qt6-forward-compat` (the 10 `static_cast<int>` sites) must be **merged/based-under** the
   floating branch FIRST. Lifting the cap before those casts are present = tree won't compile.
2. `pri/quazipdetect.pri` must stop deriving the dir from `$QT_VERSION` (see 8.1), else every Qt
   patch bump silently breaks configure.
3. Only AFTER 1+2, drop `QT_MOST` in `phoenix.pro` (keep `QT_LEAST` floor). `tools/build-with-qt.sh`
   (the raise-cap-temporarily-and-restore script) becomes obsolete once the cap is gone.
The forward-compat casts are cross-cutting: they touch autoroute/connectors/dialogs/items/sketch/
utils — the SAME files as the feature branches, so they can't be trivially cherry-picked onto a
dirty tree. This is why Phase 0 is "commit the 21 dirty files to their proper branches," not "flip
the cap."

### 8.3 CRITICAL — the dirty tree is partly a REGRESSION, not new work (verified 2026-10-24)
Investigation of the 21 dirty files against the committed branch tips shows the working tree is
NOT "new uncommitted feature work." It is a MIX of:
  (a) Pure forward-compat casts (textutils/commands/connectoritem/itembase/paletteitembase/
      resistor/sketchwidget) = identical to what is ALREADY committed on pr/qt6-forward-compat.
  (b) Connector-debug + gerber menu-rename content (applyConnectorDebugSetting,
      connectorDebugHighlightEnabled, m_viewGerberAct) = ALREADY committed on pr/connector-debug-pref
      and pr/gerber-preview respectively.
  (c) A REGRESSION: the dirty gerberparser.cpp DELETES the JIT `static const QRegularExpression`
      optimization that commit d01b2d52 ("hoist hot-path regexes to optimized statics") added.
      So the current tree, if committed as-is, would REVERT a committed perf win.
Also: there are `cline checkpoint` commits (8f2f185a/51c96fd6/b3c7a66e + their "untracked files"
parents) capturing this same 21-file blob as auto-checkpoints — these are editor session
checkpoints, NOT curated commits, and must not be treated as source of truth.
CONCLUSION: do NOT commit the dirty tree. The feature work is already committed on the pr/* branches;
the tree's diff vs develop is exactly those branches' content (correctly based on develop). The right
Phase 0 is: (1) DROP the dirty tree changes (git restore) since they're already on the pr/* branches,
(2) the casts already live on pr/qt6-forward-compat, (3) proceed to cast-first merge onto develop.

### 8.4 OWNER CLARIFICATION (2026-10-24) — working tree IS the source of truth
Owner note: the upstream Fritzing maintainer required the work be SPLIT into separate PRs to even be
considered, so the pr/* branches were an externally-driven artifact, not the desired shape. Owner's
decision: **the current working tree is what matters** — it is the authoritative "panelizer/previewer"
state. We do NOT discard anything until the rebased/recombined result reproduces the tree. The
gerber JIT-regex "static" regression noted in 8.3 is ACCEPTABLE for now ("it is what it is; can be
removed later") — do NOT try to preserve it; the tree's non-static form is fine.
Therefore cast-first plan:
  1. Create feature/qt6-modern (or merge order) with pr/qt6-forward-compat as the BASE on develop.
  2. Layer the feature branches (panelizer, gerber-preview, connector-debug) on top, preserving the
     working-tree content exactly.
  3. Verify the recombined result == current working tree (git diff must be empty).
  4. Only after parity confirmed: drop QT_MOST ceiling, keep QT_LEAST floor.
  5. Decouple quazipdetect.pri from $QT_VERSION (8.1) so floating actually works.
The 'static const QRegularExpression' optimization is explicitly de-scoped (owner OK to keep it
removed). This de-fangs the "regression" concern from 8.3.

### 8.5 STATE SNAPSHOT (2026-10-24) — restructure-safe
Authoritative working tree captured as commit `09b80178` ("snapshot: authoritative working tree"),
which is HEAD and is where `backup/authoritative-tree-20261024` points. This snapshot = the union of
the pr/* branch content as it should exist combined (casts + connector-debug + gerber menu rename +
panelizer). Scratch docs (homerun*.md, pr-reply.md, pr-split-and-response.md, INTERNAL-GAMEPLAN.md),
the 491MB `parts/` parts-DB embedded repo, and the build-unreferenced `src/partseditor/obsolete/`
(51 files) are deliberately LEFT UNTRACKED. obsolete/ is in owner's separate backup; pull later if
needed. Now safe to proceed with cast-first restructure because nothing is lost.

### 8.6 CAST-FIRST BRANCH BUILT (2026-10-24) — parity proven
`feature/qt6-modern` built off develop with cast-first ordering:
  develop(04e5bb02) → merge pr/qt6-forward-compat(CASTS, base) → pr/connector-debug-pref(KEPT)
  → pr/gerber-preview → pr/panelizer.
Verified: `git diff feature/qt6-modern backup/authoritative-tree-20261024` == EMPTY (diff-lines=0).
The restructured branch reproduces the authoritative tree byte-for-byte. This is the new source of
truth branch. `--strategy-option=theirs` was used on merges, but parity-vs-snapshot is the proof that
no cast/feature content was lost, so the earlier concern about theirs dropping casts is disproven.
STILL TODO to actually FLOAT:
  - Decouple pri/quazipdetect.pri from $QT_VERSION (see 8.1).
  - Drop QT_MOST ceiling in phoenix.pro (keep QT_LEAST).
  - tools/build-with-qt.sh becomes obsolete once cap is gone — can be deleted.

### 8.7 FLOATING ACHIEVED (2026-10-24)
Commit 6f85a105 on feature/qt6-modern:
  - phoenix.pro: removed `QT_MOST=6.5.10` and the `versionAtMost` guard (verified gone). Kept
    `QT_LEAST=6.5.3` floor only.
  - pri/quazipdetect.pri: path now `quazip_qt$$QT_MAJOR_VERSION` (was patch-pinned
    `quazip-$$QT_VERSION-1.4intuisphere`). Includes resolve because source uses
    `#include <quazip/quazip.h>` and the dir is quazip_qt6/include/quazip/*.h (verified headers exist).
VERIFIED: `qmake6 phoenix.pro` on Qt 6.11.2 configures cleanly (cfg_exit=0); generated app makefile
carries `-L .../quazip_qt6/lib -lquazip1-qt6`. This proves configure-stage floating works. Full
compile build kicked off to prove compile-stage. tools/build-with-qt.sh is now obsolete (cap gone).

### 8.8 RESOLVED — the panelizer 'TileRect' break was SELF-INFLICTED by the snapshot, NOT your code
Root cause (verified): the working tree at session start already contained a STALE, pre-98f32acf
copy of panelizer.h (old PlanePair/TileRect/cmrouter-tile form) plus a reverted gerberparser.cpp.
My `git add -A` snapshot (09b80178) committed those STALE files OVER your clean, committed feature
code, so feature/qt6-modern (built via --strategy-option=theirs over the snapshot) inherited the
stale panelizer.h that references the deleted Tile engine. That is the whole reason it failed to
compile. Your real panelizer work is CORRECT and COMPLETE:
  - pr/panelizer tip 3f29babc: panelizer.h is CLEAN (no cmrouter/tile.h). VERIFIED (grep count 0).
  - pr/gerber-preview tip d01b2d52: all 5 'static const QRegularExpression' opts intact. VERIFIED.
  - 98f32acf (parent of snapshot) is the clean panelizer refactor; commit msg: "no remaining
    references to Tile/Plane. Compile-verified ... against Qt 6.11." (matches owner: it DID build).
CORRECT REBUILD: do NOT trust the dirty snapshot for cross-cutting files. Rebuild feature/qt6-modern
from the REAL committed branch tips (cast-first), in dependency order. Confirmed real relations:
  pr/panelizer(3f29babc) ALREADY contains pr/gerber-preview(d01b2d52) [gerber_IN_panelizer=YES]
  pr/panelizer does NOT contain casts(95c9eca7) or connector(720213d3) — those stay separate siblings.
So cast-first stack = develop -> pr/qt6-forward-compat(casts) -> then merge connector-debug -> then
merge the panelizer-line (3f29babc, which already carries gerber). No more dirty-snapshot content.

## 9. What "A-class" means concretely (north star)
Green CI on modern Qt across the matrix; every open issue dispositioned; panelizer + preview
finished and golden-tested; behavior-bearing facts in tracked docs; `AGENTS.md` + contribution
guide that let a stranger land a small, tested change in one sitting. The app already has 15
years of domain logic — we are not making it smart, we are making it *maintainable and current*.

### 8.9 REBUILD COMPLETE (2026-10-24) — feature/qt6-modern-fixed is the correct source of truth
feature/qt6-modern was WRONG (built over the stale dirty snapshot, so it carried the pre-98f32acf
panelizer.h that referenced deleted Tile types). Corrected branch feature/qt6-modern-fixed rebuilt
cleanly: base = real pr/panelizer tip 3f29babc (clean panelizer + gerber statics) -> cherry-pick
casts 95c9eca7 -> cherry-pick connector 720213d3 (resolved as union of gerber+connector handlers) ->
float commit 009ed5bb (drop QT_MOST, quazip keyed on QT_MAJOR_VERSION, wire test_panelizer +
test_gerberpreview into auto.pro, gitignore artifact rules). This is now the canonical branch.
Scratch docs + obsolete/ + parts/ remain untracked. Awaiting compile build to confirm it builds.

### 8.10 FLOAT COMPILE+LINK PROVEN (2026-10-24) — binary builds on Qt 6.11.2
Clean out-of-tree build of feature/qt6-modern-fixed: 421 objects compiled, LINKED a real ELF binary
(/tmp/cfg4/Fritzing) with ZERO fatal errors, ZERO undefined refs, ldd shows 0 missing libs. Floating
proven end-to-end (configure AND compile AND link), not just configure. Fixed the earlier quazip
dangling-var bug: line 31 now 'INCLUDEPATH += QUAZIP_PATH/include' (was referencing the deleted
QUAZIP_INCLUDE_PATH, so folderutils.cpp could not find <quazip/quazip.h>). Deprecation warnings
(QDomDocument::setContent etc.) remain but are non-fatal. tools/build-with-qt.sh (temp-cap workaround)
is now obsolete since the cap is permanently gone.
