# Edit History Panel — Design

**Date:** 2026-07-18
**Component:** Retouch editor (`src/edit/`)

## Goal

Add a history panel to the retouch editor that lists every committed edit step. Clicking any row jumps the image back (or forward) to that state. The panel is toggleable from the View menu.

## Background

An undo/redo stack already exists per-tab in `RetouchTab`:

- `QVector<Adjustments> m_history` — committed adjustment snapshots
- `int m_histIndex` — current position
- `commitHistory()` — dedups via `operator==`, drops the redo branch, caps at `kMaxHistory = 60`
- `applyHistoryState()` — `m_adj = m_history[m_histIndex]`; re-render
- `undo()` / `redo()` / `canUndo()` / `canRedo()`
- signal `historyChanged(bool canUndo, bool canRedo)`

History is in-memory only (not persisted to the `.nte.json` sidecar). The panel surfaces this existing stack as a clickable list. Clicking a row before the current position acts as undo; clicking one after acts as redo — both via a single jump method.

## Components

### 1. RetouchTab — history read API + jump

Add:

- `const QVector<Adjustments>& history() const;`
- `int historyIndex() const;`
- `void jumpToHistory(int index);` — clamps `index` into range, sets `m_histIndex = index`, calls `applyHistoryState()`, emits change. Generalizes `undo()`/`redo()`.

Extend notification so the panel can rebuild and re-select. Add a signal `historyListChanged()` emitted wherever `m_history` or `m_histIndex` changes (commit, undo, redo, jump). The existing `historyChanged(bool,bool)` stays for the Edit-menu Undo/Redo actions.

### 2. Step labels — derive by diffing

Free function (in `Adjustments.h/.cpp` or a small helper):

```cpp
QString historyStepLabel(const Adjustments &prev, const Adjustments &curr);
```

Compares the two snapshots and returns the name of the field that changed: "Brightness", "Contrast", "Exposure", "Curve", "Crop", "Rotate", "Flip", "Spot Heal", etc. Row 0 (no prior) = "Original". If several fields differ in one commit, name the primary/first change; fall back to "Adjust" if nothing recognizable differs.

### 3. RetouchWindow — panel UI

- New `QDockWidget m_historyDock` containing a `QListWidget`, built in `buildHistoryDock()` (mirrors `buildDock()`), added to the same dock area as the Adjustments dock.
- Rows list history entries top-to-bottom with derived labels. The row at `historyIndex()` is selected/highlighted.
- Clicking a row calls the active tab's `jumpToHistory(row)`.

### 4. View menu toggle

In `buildViewMenu()` (called after the dock exists):

```cpp
viewMenu->addAction(m_historyDock->toggleViewAction());
```

### 5. Tab switching & wiring

RetouchWindow connects to the current tab's `historyListChanged` signal. On tab change (and when a tab becomes active), it disconnects the old tab, connects the new one, rebuilds the list from that tab's `history()`, and selects `historyIndex()`. Making a new edit after jumping back drops the redo branch (existing behavior); the panel simply rebuilds and those rows disappear.

## Non-goals

- No persistence of history (stays in-memory, matching current behavior).
- No per-step thumbnails.
- No renaming/annotating steps.

## Testing

- Manual: apply several edits, confirm rows appear with sensible labels; click an earlier row and confirm the image reverts and the selection moves; click a later row to redo; make a new edit mid-history and confirm forward rows drop; toggle the panel via View menu; switch tabs and confirm the panel reflects the active tab.
