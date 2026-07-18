# Edit History Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dockable history panel to the retouch editor that lists every committed edit step with a human-readable label, where clicking a row jumps the image to that state.

**Architecture:** Surface the undo/redo stack that already exists in `RetouchTab` (`m_history` + `m_histIndex`). Add read accessors, a `jumpToHistory(int)` method, and a `historyListChanged()` signal to `RetouchTab`. Add a pure `historyStepLabel(prev, curr)` helper that names a step by diffing two `Adjustments`. In `RetouchWindow`, add a `QDockWidget` holding a `QListWidget`, wire it to the active tab, and expose it via the View menu.

**Tech Stack:** C++17, Qt 6 Widgets, CMake/Ninja.

## Global Constraints

- No test framework exists in this repo; verification is `cmake --build` + manual runtime check. Do not add a test target.
- History is in-memory only — do not persist it to the `.nte.json` sidecar.
- Follow existing Qt idioms in `src/edit/` (raw `new` with parent ownership, `connect` lambdas guarded by `if (tab != currentTab()) return;`).
- Build command: `cmake --build build` (configure first with `cmake -S . -B build -G Ninja` if `build/` is absent).

---

### Task 1: History step labels (`historyStepLabel`)

Pure function that names a step by diffing the previous and current `Adjustments`.

**Files:**
- Modify: `src/edit/Adjustments.h` (add declaration after `applyAdjustments`, ~line 80)
- Modify: `src/edit/Adjustments.cpp` (add definition)

**Interfaces:**
- Produces: `QString historyStepLabel(const Adjustments &prev, const Adjustments &curr);` — returns the name of the primary changed field ("Brightness", "Contrast", "Highlights", "Shadows", "Saturation", "Vibrance", "Temperature", "Tint", "White Balance", "Clarity", "Sharpen", "Vignette", "Curve", "Spot Heal", "Rotate", "Flip", "Crop"), or "Adjust" if something differs but is unrecognized, or "No change" if equal.

- [ ] **Step 1: Declare the function**

In `src/edit/Adjustments.h`, add after the `applyAdjustments` declaration (after line 80):

```cpp
// Human-readable name of what changed between two committed snapshots. Returns
// the primary changed field's label (e.g. "Brightness", "Crop", "Spot Heal").
// "No change" if equal; "Adjust" if something differs but isn't recognized.
QString historyStepLabel(const Adjustments &prev, const Adjustments &curr);
```

Add `#include <QString>` to the includes at the top if not already present (it is pulled in transitively via QImage, but include it explicitly).

- [ ] **Step 2: Define the function**

In `src/edit/Adjustments.cpp`, add at the end of the file:

```cpp
QString historyStepLabel(const Adjustments &prev, const Adjustments &curr) {
    if (prev == curr) return QStringLiteral("No change");
    if (curr.brightness != prev.brightness)   return QStringLiteral("Brightness");
    if (curr.contrast != prev.contrast)       return QStringLiteral("Contrast");
    if (curr.highlights != prev.highlights)   return QStringLiteral("Highlights");
    if (curr.shadows != prev.shadows)         return QStringLiteral("Shadows");
    if (curr.saturation != prev.saturation)   return QStringLiteral("Saturation");
    if (curr.vibrance != prev.vibrance)       return QStringLiteral("Vibrance");
    if (curr.temperature != prev.temperature) return QStringLiteral("Temperature");
    if (curr.tint != prev.tint)               return QStringLiteral("Tint");
    if (curr.wbR != prev.wbR || curr.wbG != prev.wbG || curr.wbB != prev.wbB)
        return QStringLiteral("White Balance");
    if (curr.clarity != prev.clarity)         return QStringLiteral("Clarity");
    if (curr.sharpen != prev.sharpen)         return QStringLiteral("Sharpen");
    if (curr.vignette != prev.vignette)       return QStringLiteral("Vignette");
    if (curr.curve != prev.curve)             return QStringLiteral("Curve");
    if (curr.heals != prev.heals)             return QStringLiteral("Spot Heal");
    if (curr.rotationQuadrants != prev.rotationQuadrants)
        return QStringLiteral("Rotate");
    if (curr.flipH != prev.flipH || curr.flipV != prev.flipV)
        return QStringLiteral("Flip");
    if (curr.cropRect != prev.cropRect)       return QStringLiteral("Crop");
    return QStringLiteral("Adjust");
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build build`
Expected: Builds with no errors related to `Adjustments`.

- [ ] **Step 4: Commit**

```bash
git add src/edit/Adjustments.h src/edit/Adjustments.cpp
git commit -m "Add historyStepLabel to name an edit step by diffing adjustments"
```

---

### Task 2: RetouchTab history read API + jump + signal

Expose the existing history stack and add a jump-to-index method.

**Files:**
- Modify: `src/edit/RetouchTab.h` (public methods ~lines 58-63; signals ~line 73)
- Modify: `src/edit/RetouchTab.cpp` (`commitHistory`/`applyHistoryState`/`onDecodeFinished` history sites ~lines 105-163)

**Interfaces:**
- Consumes: existing `m_history` (`QVector<Adjustments>`), `m_histIndex` (`int`), `applyHistoryState()`.
- Produces:
  - `const QVector<Adjustments> &history() const;`
  - `int historyIndex() const;`
  - `void jumpToHistory(int index);`
  - signal `void historyListChanged();` — emitted whenever `m_history` or `m_histIndex` changes.

- [ ] **Step 1: Add accessors and jump declaration to the header**

In `src/edit/RetouchTab.h`, in the public section right after the `redo()`/`canUndo()`/`canRedo()` block (after line 61), add:

```cpp
    const QVector<Adjustments> &history() const { return m_history; }
    int historyIndex() const { return m_histIndex; }
    void jumpToHistory(int index); // set position to index and apply it
```

- [ ] **Step 2: Add the signal to the header**

In `src/edit/RetouchTab.h`, in the `signals:` block after `historyChanged(...)` (after line 73), add:

```cpp
    void historyListChanged(); // history entries or current index changed
```

- [ ] **Step 3: Emit historyListChanged from existing history sites**

In `src/edit/RetouchTab.cpp`:

In `onDecodeFinished()`, after line 107 (`emit historyChanged(false, false);`), add:
```cpp
    emit historyListChanged();
```

In `commitHistory()`, add `emit historyListChanged();` immediately after each `emit historyChanged(...)` call (there are two: the early-return path ~line 125 and the end path ~line 137).

In `applyHistoryState()`, after line 146 (`emit historyChanged(canUndo(), canRedo());`), add:
```cpp
    emit historyListChanged();
```

- [ ] **Step 4: Implement jumpToHistory**

In `src/edit/RetouchTab.cpp`, add after `redo()` (after line 163):

```cpp
void RetouchTab::jumpToHistory(int index) {
    if (m_commitTimer) m_commitTimer->stop();
    commitHistory(); // capture any in-progress change first
    if (m_history.isEmpty()) return;
    index = qBound(0, index, m_history.size() - 1);
    if (index == m_histIndex) return;
    m_histIndex = index;
    applyHistoryState();
}
```

Ensure `<QtGlobal>` is available for `qBound` (it is pulled in via Qt headers already used; no new include needed).

- [ ] **Step 5: Build to verify it compiles**

Run: `cmake --build build`
Expected: Builds with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/edit/RetouchTab.h src/edit/RetouchTab.cpp
git commit -m "Expose RetouchTab history list, index, and jumpToHistory"
```

---

### Task 3: History dock UI in RetouchWindow

Add the dock, populate it from the active tab, and make clicks jump.

**Files:**
- Modify: `src/edit/RetouchWindow.h` (forward decls ~line 21; method decls ~line 48; members ~line 63)
- Modify: `src/edit/RetouchWindow.cpp` (setup call ~line 159; new `buildHistoryDock()` + `refreshHistoryPanel()`; per-tab connect ~line 557; `onTabChanged` ~line 616)

**Interfaces:**
- Consumes: `RetouchTab::history()`, `historyIndex()`, `jumpToHistory(int)`, signal `historyListChanged()`; `historyStepLabel(prev, curr)` from Task 1.
- Produces: private `void buildHistoryDock();`, `void refreshHistoryPanel();`, members `QDockWidget *m_historyDock`, `QListWidget *m_historyList`.

- [ ] **Step 1: Add forward declaration and member/method decls to the header**

In `src/edit/RetouchWindow.h`:

Near the other Qt forward declarations (after `class QDockWidget;`, line 21), add:
```cpp
class QListWidget;
```

In the private methods area, after `void buildViewMenu();` (line 64), add:
```cpp
    void buildHistoryDock();
    void refreshHistoryPanel(); // rebuild the list from the current tab
```

Near the `m_adjustmentsDock` member (line 63), add:
```cpp
    QDockWidget *m_historyDock = nullptr;
    QListWidget *m_historyList = nullptr;
```

- [ ] **Step 2: Include QListWidget in the .cpp**

In `src/edit/RetouchWindow.cpp`, add near the other Qt widget includes at the top:
```cpp
#include <QListWidget>
```

- [ ] **Step 3: Implement buildHistoryDock and refreshHistoryPanel**

In `src/edit/RetouchWindow.cpp`, add after `buildDock()` ends (after line 501):

```cpp
void RetouchWindow::buildHistoryDock() {
    auto *dock = new QDockWidget("History", this);
    m_historyDock = dock;
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_historyList = new QListWidget;
    m_historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_historyList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                RetouchTab *tab = currentTab();
                if (tab) tab->jumpToHistory(m_historyList->row(item));
            });
    dock->setWidget(m_historyList);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    // Stack under the Adjustments dock as a tab if both are on the right.
    if (m_adjustmentsDock) tabifyDockWidget(m_adjustmentsDock, dock);
}

void RetouchWindow::refreshHistoryPanel() {
    if (!m_historyList) return;
    RetouchTab *tab = currentTab();
    QSignalBlocker block(m_historyList);
    m_historyList->clear();
    if (!tab || !tab->isReady()) return;
    const QVector<Adjustments> &hist = tab->history();
    for (int i = 0; i < hist.size(); ++i) {
        QString label = (i == 0) ? QStringLiteral("Original")
                                 : historyStepLabel(hist[i - 1], hist[i]);
        m_historyList->addItem(label);
    }
    int cur = tab->historyIndex();
    if (cur >= 0 && cur < m_historyList->count())
        m_historyList->setCurrentRow(cur);
}
```

- [ ] **Step 4: Call buildHistoryDock during setup**

In `src/edit/RetouchWindow.cpp`, in the setup sequence, change the order so the history dock is built before the View menu. After line 159 (`buildDock();`) and before line 160 (`buildViewMenu();`), insert:
```cpp
    buildHistoryDock();
```

- [ ] **Step 5: Add the View-menu toggle**

In `src/edit/RetouchWindow.cpp`, in `buildViewMenu()`, after the Adjustments toggle line (line 177), add:
```cpp
    if (m_historyDock) viewMenu->addAction(m_historyDock->toggleViewAction());
```

- [ ] **Step 6: Refresh the panel from the per-tab history signal**

In `src/edit/RetouchWindow.cpp`, in `openPhoto()`, right after the existing `historyChanged` connect block (after line 562), add:
```cpp
    connect(tab, &RetouchTab::historyListChanged, this, [this, tab] {
        if (tab != currentTab()) return;
        refreshHistoryPanel();
    });
```

Also, in the `decoded` connect lambda, after `syncDockFromTab();` (line 534), add `refreshHistoryPanel();` so the panel populates once decode finishes.

- [ ] **Step 7: Refresh the panel on tab switch**

In `src/edit/RetouchWindow.cpp`, in `onTabChanged(int)`, after `syncDockFromTab();` (line 623), add:
```cpp
    refreshHistoryPanel();
```

- [ ] **Step 8: Build to verify it compiles**

Run: `cmake --build build`
Expected: Builds with no errors.

- [ ] **Step 9: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Add History dock with clickable steps and View-menu toggle"
```

---

### Task 4: Manual verification

**Files:** none (runtime check).

- [ ] **Step 1: Launch the app and open a photo in the retouch editor**

Run the built binary, open a RAW/NEF in the retouch window.

- [ ] **Step 2: Verify history rows appear with labels**

Adjust several sliders (brightness, contrast), apply a crop, add a spot heal. Confirm the History dock shows "Original" plus a labeled row per committed step (e.g. "Brightness", "Crop", "Spot Heal"), with the newest at the bottom selected.

- [ ] **Step 3: Verify click-to-jump (both directions)**

Click an earlier row → image reverts to that state, dock values update, that row becomes selected. Click a later row → image redoes forward.

- [ ] **Step 4: Verify redo-branch drop**

Jump back a few steps, then move a slider. Confirm the forward rows disappear and a new row is appended.

- [ ] **Step 5: Verify View-menu toggle and tab switching**

Toggle "History" off/on from the View menu. Open a second photo in another tab, switch between tabs, and confirm the panel reflects each tab's own history.
