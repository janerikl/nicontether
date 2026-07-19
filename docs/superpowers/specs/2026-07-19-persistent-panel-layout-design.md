# Persistent Panel Layout + Reset Panels

**Date:** 2026-07-19
**Status:** Approved design

## Goal

When the user rearranges panels in the main window, those changes should
persist across application restarts. Provide a "Reset Panels" action in the
View menu to return to the default layout.

Persistence covers: dock/undock & reposition, resize (splitter widths),
show/hide, and main window size/position.

## Context

- `RetouchWindow` (`src/edit/RetouchWindow.cpp/.h`) is the single `QMainWindow`.
- Persistent panels are `QDockWidget`s:
  - `m_adjustmentsDock`, `m_historyDock`, `m_levelsDock` — user-controlled.
  - `m_maskDock` — visibility driven by the active tool (Mask).
  - `m_controlsDock` — visibility driven by mode (Tether).
- Panels/toolbars: `m_toolsBar` (Tools), the "Main" toolbar, `m_tetherToolBar`.
- `QApplication` sets org/app name to `"NikonTether"` (`src/main.cpp:13-14`),
  so a default `QSettings` resolves to that scope. `QSettings` is already used
  by `RecentSessions` and `ExportPreset` — mirror those patterns.
- No `closeEvent` override currently exists.

## Design

### 1. Object names (prerequisite)

`QMainWindow::saveState()`/`restoreState()` require a unique `setObjectName()`
on every `QDockWidget` and `QToolBar`. Add stable names to all five docks and
all three toolbars (e.g. `"adjustmentsDock"`, `"historyDock"`, `"levelsDock"`,
`"maskDock"`, `"controlsDock"`, `"toolsBar"`, `"mainToolBar"`,
`"tetherToolBar"`). These are internal keys only; not user-visible.

### 2. Save on exit

Add `void closeEvent(QCloseEvent *) override;` to `RetouchWindow`. In it:

```cpp
QSettings s;
s.setValue("window/geometry", saveGeometry());
s.setValue("window/state", saveState());
QMainWindow::closeEvent(event);
```

### 3. Restore on launch

In the constructor, after all `buildX()` calls have created the docks/toolbars
(and before mode/tool visibility is applied), restore:

```cpp
QSettings s;
if (s.contains("window/geometry"))
    restoreGeometry(s.value("window/geometry").toByteArray());
if (s.contains("window/state"))
    restoreState(s.value("window/state").toByteArray());
```

### 4. Mode/tool-driven visibility (Masks + Controls)

`restoreState()` restores *position and size* for `m_maskDock` and
`m_controlsDock`, but their **visibility must remain app-controlled**. After
restore, the existing mode/tool logic (`setMode`, `setDockEnabled`, and the
tool-activation path that shows/hides the mask dock) runs and re-asserts the
correct visibility for these two docks. Ensure restore happens *before* that
logic so the app has the final say on when Masks/Controls appear.

The three normal panels (Adjustments, History, Levels) persist fully,
including show/hide state.

### 5. Default arrangement extraction

Extract the current default docking arrangement (the `addDockWidget`,
`tabifyDockWidget`, `splitDockWidget`, and initial `hide()` calls currently
spread across the `buildX` methods) into a single reusable method, e.g.
`applyDefaultDockLayout()`. Both first launch (no saved state) and Reset
Panels call this one code path.

### 6. Reset Panels action

Add a "Reset Panels" `QAction` to `buildViewMenu()`. On trigger:

1. Remove saved panel state: `QSettings s; s.remove("window/state");`
   (leave `window/geometry` untouched — reset is **panels only**, not window
   size/position).
2. Call `applyDefaultDockLayout()` to restore the default arrangement live.
3. Re-assert mode/tool visibility for Masks/Controls.

## Non-goals

- No reset of window size/position (panels-only reset).
- No per-panel reset; reset is all-or-nothing.
- No new persisted settings beyond geometry + state blobs.

## Testing

- Rearrange panels (dock elsewhere, resize, hide Adjustments), close, relaunch
  → layout restored.
- Move window, relaunch → window position/size restored.
- Leave Mask tool active with mask dock open, close, relaunch into a non-mask
  context → mask dock not visible, but reappears docked in its remembered spot
  when the Mask tool is activated.
- View → Reset Panels → default arrangement returns; window size/position
  unchanged.
- First launch with no saved settings → default layout, no Qt warnings.
