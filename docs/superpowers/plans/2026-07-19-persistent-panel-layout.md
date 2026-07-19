# Persistent Panel Layout + Reset Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist the main window's panel layout (dock positions, sizes, tabbing, show/hide) and window geometry across restarts, and add a "Reset Panels" action to the View menu.

**Architecture:** Use `QMainWindow::saveState()`/`restoreState()` and `saveGeometry()`/`restoreGeometry()`, persisted as `QByteArray` blobs in `QSettings` (the app's existing `"NikonTether"` scope). All docks/toolbars get unique object names (a Qt prerequisite for state save/restore). Save on `closeEvent`; restore near the end of the constructor. A single `applyDefaultDockLayout()` method encodes the default arrangement, reused by first-launch and Reset Panels. Masks/Controls docks keep app-controlled visibility.

**Tech Stack:** C++17, Qt 6 (`QMainWindow`, `QDockWidget`, `QToolBar`, `QSettings`). Build: CMake + Ninja in `build/`.

## Global Constraints

- App identity is already set in `src/main.cpp:13-14` (`setApplicationName("NikonTether")`, `setOrganizationName("NikonTether")`), so a default-constructed `QSettings s;` resolves to that scope. Always use `QSettings s;` with no args — mirror `src/edit/RecentSessions.cpp`.
- QSettings keys: `"window/geometry"` and `"window/state"`.
- Every `QDockWidget` and `QToolBar` MUST have a unique `setObjectName()` or Qt's `saveState()` emits a runtime warning and cannot restore reliably.
- No GUI unit-test harness exists in this project. Verification for each task is: compile clean with `cmake --build build`, then manual launch/interaction where noted.

**Build command (used throughout):**
```bash
cmake --build build
```
Expected: `ninja: no work to do.` or a successful link ending in the `nikontether` binary with no errors.

**Run command (used throughout):**
```bash
./build/nikontether
```

---

### Task 1: Give every dock and toolbar a stable object name

Qt cannot save/restore dock state without unique object names. This task adds them and changes nothing else observable.

**Files:**
- Modify: `src/edit/RetouchWindow.cpp` (constructor + the four `buildX` dock methods)

**Interfaces:**
- Produces: object names used implicitly by `saveState()`/`restoreState()` in later tasks. Names: `"mainToolBar"`, `"toolsBar"`, `"tetherToolBar"`, `"adjustmentsDock"`, `"historyDock"`, `"levelsDock"`, `"maskDock"`, `"controlsDock"`.

- [ ] **Step 1: Name the Main toolbar.** In the constructor, immediately after `auto *toolbar = addToolBar("Main");` (line ~223):

```cpp
    auto *toolbar = addToolBar("Main");
    toolbar->setObjectName("mainToolBar");
```

- [ ] **Step 2: Name the Tools toolbar.** In `buildToolPanel()`, after `m_toolsBar = new QToolBar("Tools", this);` (line ~401):

```cpp
    m_toolsBar = new QToolBar("Tools", this);
    m_toolsBar->setObjectName("toolsBar");
```

- [ ] **Step 3: Name the Tether toolbar.** In the constructor, after `m_tetherToolBar = addToolBar("Tether");` (line ~347):

```cpp
    m_tetherToolBar = addToolBar("Tether");
    m_tetherToolBar->setObjectName("tetherToolBar");
```

- [ ] **Step 4: Name the Controls dock.** In the constructor, after `m_controlsDock = new QDockWidget("Controls", this);` (line ~341):

```cpp
    m_controlsDock = new QDockWidget("Controls", this);
    m_controlsDock->setObjectName("controlsDock");
```

- [ ] **Step 5: Name the Adjustments dock.** In `buildDock()`, after `m_adjustmentsDock = dock;` (line ~623):

```cpp
    m_adjustmentsDock = dock;
    dock->setObjectName("adjustmentsDock");
```

- [ ] **Step 6: Name the History dock.** In `buildHistoryDock()`, after `m_historyDock = dock;` (line ~751):

```cpp
    m_historyDock = dock;
    dock->setObjectName("historyDock");
```

- [ ] **Step 7: Name the Levels dock.** In `buildLevelsDock()`, after `m_levelsDock = dock;` (line ~785):

```cpp
    m_levelsDock = dock;
    dock->setObjectName("levelsDock");
```

- [ ] **Step 8: Name the Masks dock.** In `buildMaskDock()`, after `m_maskDock = dock;` (line ~821):

```cpp
    m_maskDock = dock;
    dock->setObjectName("maskDock");
```

- [ ] **Step 9: Build.**

Run: `cmake --build build`
Expected: successful build, no errors.

- [ ] **Step 10: Run and confirm no Qt object-name warnings.**

Run: `./build/nikontether` (close it after the window appears)
Expected: window opens with the default layout unchanged; no `QMainWindow::saveState(): ... requires all ... objectName` warnings on stderr. (There is nothing to save yet, but naming must not regress anything.)

- [ ] **Step 11: Commit.**

```bash
git add src/edit/RetouchWindow.cpp
git commit -m "Add object names to docks and toolbars for state persistence"
```

---

### Task 2: Extract the default dock arrangement into applyDefaultDockLayout()

Create one reusable method that positions the already-created docks/toolbar into the default arrangement and lets mode chrome assert final visibility. This is used by Reset (Task 4) and is the safe fallback when there is no saved state (Task 3).

**Files:**
- Modify: `src/edit/RetouchWindow.h` (declare method)
- Modify: `src/edit/RetouchWindow.cpp` (define method)

**Interfaces:**
- Produces: `void applyDefaultDockLayout();` — re-docks Levels/Adjustments/History/Masks/Controls to the right in the default tab/split arrangement, returns Tools toolbar to the left, sets default visibility, then calls `applyModeChrome(...)` for the current mode.
- Consumes: existing members `m_levelsDock`, `m_adjustmentsDock`, `m_historyDock`, `m_maskDock`, `m_controlsDock`, `m_toolsBar`, `m_tetherModeAction`, and existing method `applyModeChrome(Mode)`.

- [ ] **Step 1: Declare the method.** In `src/edit/RetouchWindow.h`, next to the other dock-building declarations (after `void buildViewMenu();`, line ~101):

```cpp
    void buildViewMenu();
    void applyDefaultDockLayout(); // re-apply the default dock arrangement (used on first launch + Reset Panels)
```

- [ ] **Step 2: Define the method.** In `src/edit/RetouchWindow.cpp`, add after `buildMaskDock()`'s closing brace (search for the end of `buildMaskDock`; place it before the next method). Include `<QDockWidget>` and `<QToolBar>` are already included.

```cpp
// Re-apply the default dock arrangement to the (already-created) docks: all
// editing docks on the right, Levels split above the Adjustments/History/Masks
// tab group, Tools toolbar on the left. Mask stays hidden; applyModeChrome then
// asserts mode-driven visibility (Masks/Controls). Used on first launch (no
// saved state) and by Reset Panels.
void RetouchWindow::applyDefaultDockLayout() {
    for (QDockWidget *d : {m_levelsDock, m_adjustmentsDock, m_historyDock,
                           m_maskDock, m_controlsDock}) {
        if (d) {
            d->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, d);
        }
    }
    if (m_adjustmentsDock && m_historyDock)
        tabifyDockWidget(m_adjustmentsDock, m_historyDock);
    if (m_adjustmentsDock && m_maskDock)
        tabifyDockWidget(m_adjustmentsDock, m_maskDock);
    if (m_levelsDock && m_adjustmentsDock)
        splitDockWidget(m_levelsDock, m_adjustmentsDock, Qt::Vertical);
    if (m_toolsBar)
        addToolBar(Qt::LeftToolBarArea, m_toolsBar);

    // Default visibility for the persistent editing docks; Masks hidden.
    if (m_adjustmentsDock) m_adjustmentsDock->show();
    if (m_historyDock)     m_historyDock->show();
    if (m_levelsDock)      m_levelsDock->show();
    if (m_maskDock)        m_maskDock->hide();

    // Let mode/tool chrome have the final say on Masks/Controls/Tools visibility.
    const bool tether = m_tetherModeAction && m_tetherModeAction->isChecked();
    applyModeChrome(tether ? Mode::Tether : Mode::Retouch);
}
```

- [ ] **Step 3: Build.**

Run: `cmake --build build`
Expected: successful build, no errors. (Method compiles but is not yet called — that is fine.)

- [ ] **Step 4: Commit.**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Add applyDefaultDockLayout() for reusable default panel arrangement"
```

---

### Task 3: Save layout on close, restore on launch

Persist geometry + dock state on exit and restore them at startup. When no saved state exists, fall back to the default arrangement. After restore, re-assert app-controlled visibility for the Masks and Controls docks so a persisted-visible state never leaks into the wrong context.

**Files:**
- Modify: `src/edit/RetouchWindow.h` (declare `closeEvent`)
- Modify: `src/edit/RetouchWindow.cpp` (add include, restore in constructor, define `closeEvent`)

**Interfaces:**
- Consumes: `applyDefaultDockLayout()` (Task 2), object names (Task 1), `QSettings` keys `"window/geometry"`, `"window/state"`.
- Produces: `protected: void closeEvent(QCloseEvent *event) override;`

- [ ] **Step 1: Add includes.** Near the other `#include <Q...>` lines at the top of `src/edit/RetouchWindow.cpp`, add:

```cpp
#include <QSettings>
#include <QCloseEvent>
```

- [ ] **Step 2: Declare closeEvent.** In `src/edit/RetouchWindow.h`, add a `protected:` section (place it just before the `private:` section, after the `setMode` public declaration block around line 43):

```cpp
protected:
    void closeEvent(QCloseEvent *event) override;

private:
```
(If a `private:` label already immediately follows, replace it with the block above so there is exactly one `private:`.)

- [ ] **Step 3: Restore in the constructor.** In `src/edit/RetouchWindow.cpp`, replace the final line of the constructor `setMode(Mode::Retouch);` (line ~375) with:

```cpp
    // Restore saved window geometry + dock layout, or fall back to defaults.
    // Restore runs *after* setMode so persisted show/hide of the editing docks
    // wins; then Masks/Controls visibility is re-asserted by app logic.
    setMode(Mode::Retouch);
    QSettings settings;
    if (settings.contains("window/state")) {
        if (settings.contains("window/geometry"))
            restoreGeometry(settings.value("window/geometry").toByteArray());
        restoreState(settings.value("window/state").toByteArray());
        // Masks/Controls visibility is app-controlled, never persisted-visible.
        if (m_maskDock)     m_maskDock->hide();
        if (m_controlsDock) m_controlsDock->hide();
    } else {
        applyDefaultDockLayout();
    }
```

- [ ] **Step 4: Define closeEvent.** In `src/edit/RetouchWindow.cpp`, add near the other lifecycle methods (e.g. right after `setMode()` definition, around line 1095):

```cpp
void RetouchWindow::closeEvent(QCloseEvent *event) {
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    QMainWindow::closeEvent(event);
}
```

- [ ] **Step 5: Build.**

Run: `cmake --build build`
Expected: successful build, no errors.

- [ ] **Step 6: Manual verification — layout persists.**

```bash
./build/nikontether
```
Then: drag the Adjustments dock to the LEFT edge, resize it wider, and hide the History dock via View menu. Close the window. Relaunch:
```bash
./build/nikontether
```
Expected: Adjustments is on the left at its new width; History is still hidden. No Qt state warnings on stderr.

- [ ] **Step 7: Manual verification — window geometry persists.**

Move and resize the window, close, relaunch.
Expected: window reopens at the same position and size.

- [ ] **Step 8: Manual verification — Masks stays app-controlled.**

With the Mask tool active and the Masks dock showing, close the app. Relaunch (starts in Retouch mode with no mask tool active).
Expected: the Masks dock is NOT visible on launch, but its remembered dock position is used when the Mask tool is next activated.

- [ ] **Step 9: Manual verification — clean first run.**

```bash
rm -f ~/.config/NikonTether/NikonTether.conf
./build/nikontether
```
Expected: default layout appears (Levels top-right, Adjustments/History tabbed below, Tools bar on the left), no warnings.

- [ ] **Step 10: Commit.**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Persist and restore window geometry and panel layout across restarts"
```

---

### Task 4: Add "Reset Panels" to the View menu

Add a menu action that clears the saved panel state and re-applies the default arrangement live, without touching window geometry.

**Files:**
- Modify: `src/edit/RetouchWindow.cpp` (`buildViewMenu()`)

**Interfaces:**
- Consumes: `applyDefaultDockLayout()` (Task 2), `QSettings` key `"window/state"`.

- [ ] **Step 1: Add the action.** In `buildViewMenu()` (line ~380), before the final closing brace (after the `viewMenu->addAction(filmstripAction);` line), add:

```cpp
    viewMenu->addSeparator();
    auto *resetPanelsAction = new QAction("Reset Panels", this);
    connect(resetPanelsAction, &QAction::triggered, this, [this] {
        QSettings settings;
        settings.remove("window/state"); // panels only; leave window/geometry
        applyDefaultDockLayout();
    });
    viewMenu->addAction(resetPanelsAction);
```

- [ ] **Step 2: Build.**

Run: `cmake --build build`
Expected: successful build, no errors.

- [ ] **Step 3: Manual verification.**

```bash
./build/nikontether
```
Drag Adjustments to the left and hide History. Then View → Reset Panels.
Expected: docks snap back to the default arrangement (Adjustments/History tabbed on the right, Levels above, Tools bar left, Masks hidden); the window itself does not move or resize. Close and relaunch → default layout persists (because the saved state was cleared).

- [ ] **Step 4: Commit.**

```bash
git add src/edit/RetouchWindow.cpp
git commit -m "Add Reset Panels action to the View menu"
```

---

## Self-Review

**Spec coverage:**
- Dock/reposition, resize, tabbing → `saveState`/`restoreState` (Task 3), enabled by object names (Task 1). ✓
- Show/hide persists for Adjustments/History/Levels → restore runs after `setMode` (Task 3, Step 3). ✓
- Window size/position → `saveGeometry`/`restoreGeometry` (Task 3). ✓
- Masks/Controls position persists, visibility app-controlled → explicit hide after restore + `applyModeChrome` (Tasks 2, 3). ✓
- Default arrangement extracted + reused → `applyDefaultDockLayout()` (Task 2). ✓
- Reset Panels in View menu, panels-only → Task 4 removes only `"window/state"`. ✓

**Placeholder scan:** none — all steps contain concrete code and commands.

**Type consistency:** `applyDefaultDockLayout()` declared (Task 2 Step 1) and defined (Task 2 Step 2) with matching signature; called in Tasks 3 and 4. `closeEvent(QCloseEvent*)` declared and defined consistently. QSettings keys `"window/geometry"`/`"window/state"` used identically in save (Task 3), restore (Task 3), and reset (Task 4).
