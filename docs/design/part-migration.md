# Part Migration — design & reference

> Durable reference for the **Part Migration** feature: how an obsolete part in a saved sketch is
> detected, routed, presented in the *Part Migration* dialog, and swapped to its replacement. This is
> the "how it works and why" doc; dated working notes live in `docs/internal/panelizer-progress.md`.
> Anchors below are `file:line` at the time of writing (Session 11) — re-derive line numbers if the
> files have shifted.

## 1. What "obsolete" means, and the replacedby chain

A part is **obsolete** when its `.fzp` carries a non-empty `<version replacedby="…">`. That single fact
is the whole obsolescence predicate:

- `ModelPart::isObsolete()` (`src/model/modelpart.cpp:912`) → `!replacedby().isEmpty()`.
- `ItemBase::isObsolete()` (`src/items/itembase.cpp:1621`) delegates to the model part.
- `MainWindow::findReplacedby()` (`src/mainwindow/mainwindow_menu.cpp:4086`) returns the **immediate**
  replacement `ModelPart` (one hop), or `nullptr` when the part isn't replaced **or the target isn't
  installed**.

**Stepwise (one hop at a time).** A `v1 → v2 → v3` chain is *not* collapsed: `findReplacedby` returns
only the *immediate* successor. A `v1` instance is offered `v2` (with `v1→v2`'s history mode); the
`v2→v3` step only surfaces once the instance actually sits on `v2`. `hasFurtherRevision` (computed in
`routeHistoryMigrations` via `findReplacedby(newPart) != nullptr`) lets the dialog hint that another
step will follow. This is deliberate — each step keeps its *own* mode instead of inheriting the final
part's history.

## 2. The three migration modes (`computeEffectiveMode`)

Each replacement part carries an FZP `history` list of `HistoryEntry` records, each tagged `mode`
(`src/model/modelpartshared.h:55-57`). The effective mode for a part is the **most insistent** mode
present (`MigrationHandler::computeEffectiveMode`):

| Mode | predicate | behaviour |
|------|-----------|-----------|
| `required` (`silent`) | `isRequired()` | **auto-applied silently on load**, no dialog, just a status message |
| `recommended` (`forced`; the **default** when a part has no history) | `isRecommended()` | **always prompts** on load/manual; **cannot be silenced** |
| `optional` (`ask`) | `isOptional()` | prompts when relevant; **silenceable** via "keep and don't ask again" |

Priority: **recommended > optional > required** — a change that needs review (`recommended`) is never
auto-applied even if another entry says `required`. A part with **no history at all** is `recommended`
(the classic obsolescence case, e.g. the `DB25M` example).

## 3. End-to-end flow: load → route → queue → dialog

1. **Collect.** On file load (`TriggerContext::Load`) and on manual update, `MainWindow` scans every
   view: `collectObsoleteAcrossViews()` (`mainwindow_menu.cpp:4108`) dedupes obsolete items by their
   shared cross-view `layerKinChief()->id()`.
2. **Route.** `routeHistoryMigrations(items, context)` (`mainwindow_menu.cpp:4128`) decides, per item,
   whether to queue it into the dialog (or auto-apply), keyed on the replacement's effective mode and
   the trigger context (`Load` / `Drop` / `ManualUpdate`). "Mixed" (the sketch already contains the
   replacement module) gates the soft modes on `Drop`. Items with **no resolvable replacement** are
   returned to the caller in `rest` (not queued).
3. **Queue.** `MigrationHandler::queueMigration(...)` appends a `MigrationInfo` (unique by chief id —
   dedup guards a re-click), capturing old/new module ids, titles/versions, the relevant history
   subset, `effectiveMode`, `hasFurtherRevision`, and `reason`.
4. **Process.** `MigrationHandler::processMigrations()` splits off `required` parts and **auto-applies**
   them via `swapObsoleteDirect` (silent, one undoable command, status message), sorts the rest
   deterministically (by `oldModuleID`, then `instanceTitle`), resets the session, and opens the dialog
   for whatever remains.

## 4. The dialog UI

`createMigrationDialog()` builds a **non-modal**, self-contained `QDialog` (`objectName`
`partMigrationDialog`, `WA_DeleteOnClose`) parented to the `MainWindow`. Widgets (each with an object
name for the GUI probes):

- **counter** (`migrationCounterLabel`) — "Part X of Y"; hidden for a single-part session.
- **title** (`migrationTitleLabel`) — instance title + a live state suffix ("showing new version",
  "updated to new version", "keeping old version — won't ask again", …).
- **reason** (`migrationReasonLabel`) — why this part is being shown.
- **change history** (`migrationHistoryLabel`) — the relevant `HistoryEntry` list rendered to HTML
  (date/author/mode tag), inside a `QScrollArea`.
- **version chooser** — three radios in a `QButtonGroup`: **Old**, **Keep … and don't ask again**
  (only shown when `effectiveMode == "optional"`), **New**. Default selection is **Old**: a part keeps
  the version it loaded with until the user actually picks a radio.
- **actions** — `Close`, `Update all`, `Previous`, `Next`. For a **single**-part session
  (`count == 1`) `Update all`/`Previous`/`Next` are all hidden (`setVisible(count > 1 …)`), so **Close**
  is the *only* control; Prev/Next additionally hide at the first/last part of a multi-part session.

`updateDialogForCurrentMigration()` re-renders the current part: it first `reconcileStateFromSketch()`
(so an external Undo/Redo that flipped the part old↔new is reflected), then updates counter/title/
reason/history and re-checks the matching radio **without** re-triggering a swap (`QSignalBlocker`).

## 5. Decisions are terminal (the Session-11 behaviour fix)

`onVersionChosen()` (`migrationhandler.cpp`) is the **only** place a part is actually swapped, reverted,
or (un)silenced. Choosing a radio **is the decision** for that part:

- **New** → `swapToNew(info)` (real swap) + clear any silence;
- **Keep** → revert to old + `applySilence(info)` (persist `silencedDate`, only for `optional`);
- **Old** → revert to old + clear silence.

It then sets `info.decided = true` and calls **`processNextMigration()`**, which advances to the next
*undecided* part (wrapping) and **closes the dialog once every part has a decision**.

**This is the core fix.** Previously `onVersionChosen` only called `updateDialogForCurrentMigration()`
— it re-rendered the *same* panel. For the common **single-part** case (e.g. a lone `recommended` part)
the "keep / don't ask" radio is hidden (`canSilence` is true only for `optional`) *and* Update-all/
Previous/Next are hidden (`count == 1`), leaving only **Close** as a control — so a click on
**New** looked like it *did nothing* and the file re-prompted on every open. Now a radio choice is a
decision that **advances or closes**, so **New** reads as a complete action.

`goToPreviousMigration` / `goToNextMigration` are pure navigation and **never** revert. **Close** keeps
whatever version each part is showing (it does not revert). **Update all** swaps every remaining obsolete
part to its new version in one command, then closes.

> **Recommended parts still re-ask by design.** `info.decided` is **per-session, not persisted** — the
> only durable state is `silencedDate`, and `recommended` parts can't be silenced. So a `recommended`
> part prompts every load **until the user saves the migrated sketch** (the swap becomes part of the
> saved file). This is intended, not a bug.

## 6. The swap machinery

`swapToNew` → `MainWindow::swapPartForMigration(item, newModuleID)`
(`mainwindow_menu.cpp:4358`): resolves the target `ModelPart` via `retrieveModelPart` (returns `0` →
no-op if it isn't installed), then `swapSelectedAuxAux(...)` ports the layers/connectors and
`MigrationHandler::portObsoleteSpecialProps(...)` re-applies instance values that a bare module-id swap
loses — **resistance** and **LED colour**, read from the *instance* local props (so a user's "Yellow
(592nm)" survives, and a migrated resistor keeps its 330k instead of resetting to the default 220Ω).
The swap pushes **one** `QUndoCommand`; `info.swapStackIndex` records where, and `revertToOld` unwinds
it precisely when it's still on top (`canUndoOwnSwap`), else swaps forward back to old.

## 7. Styling (the Session-11 look fix)

The app stylesheet (`fritzing.qss`) is applied **per-MainWindow**, so it cascades by widget type/parent.
The migration dialog's labels are direct `QDialog` children (matching no rule in that stylesheet), and
the change-history label sits in a `QScrollArea`'s plain `QWidget` viewport — so under a **dark system
palette** they fell back to light-on-dark (grey-on-grey text, black history box).

**Fix:** the dialog sets its **own scoped stylesheet** (`createMigrationDialog`) with object-name-scoped
rules (parent-independent), forcing dark `#333333` text and a white history box regardless of theme. The
old `QPalette`/`notePalette` hack was removed (a stylesheet `color` rule overrides `QPalette` anyway), as
was the dead `#include <QPalette>`. **`fritzing.qss` was not modified** — the fix is localized to
`migrationhandler.cpp`; the global stylesheet is left at develop.

## 8. Verification

- **No automated test drives the dialog** (it's exercised manually / via `-ftesting` + the
  `FProbeMigrationDialog` probe, which reports `open` from `dialog->isVisible()`). The dialog logic is
  covered by code review + a headless forced-dark-palette style probe (all labels resolve to
  `rgb(51,51,51)`; history bg `#ffffff`), not a golden.
- **Manual smoke test (passed):** open a sketch with an obsolete `recommended` part → dialog shows the
  part on **Old**; click **New:…** → the part swaps to its replacement **and the dialog closes**; closing
  then prompts to **save** (the sketch is dirty); saving persists the new part so it stops re-flagging.
- The broader suite stays green (8 Boost + 2 QtTest suites) — no migration changes touch them.

## 9. Key files

| File | Role |
|------|------|
| `src/sketch/migrationhandler.{h,cpp}` | the dialog + per-part decision/swap logic (the whole feature lives here) |
| `src/mainwindow/mainwindow_menu.cpp` | `findReplacedby`, `collectObsoleteAcrossViews`, `routeHistoryMigrations`, `swapPartForMigration`, `swapObsoleteDirect` |
| `src/model/modelpart.cpp`, `src/model/modelpartshared.h` | `isObsolete`/`replacedby`, `HistoryEntry` + mode predicates |
| `src/items/itembase.cpp` | `isObsolete`, obsolete-badge + silence surfacing |
| `src/testing/FProbeMigrationDialog.cpp` | the `-ftesting` probe (`/MigrationDialog/read`, `.../write/<action>`) |
