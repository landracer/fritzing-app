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
