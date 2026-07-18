# Unified Tether + Retouch Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse the app's two top-level windows into one — `RetouchWindow` becomes the sole window, with tethering reachable as a full-view mode from its top navigation, and `MainWindow` is retired.

**Architecture:** Extract all tether UI + camera ownership out of `MainWindow` into a new self-contained `TetherView` QWidget. `RetouchWindow` hosts a central `QStackedWidget` with two pages (Retouch editing tabs / `TetherView`), a shared filmstrip below both, and two mutually-exclusive toolbar buttons (Tether | Retouch) that swap the page and per-mode chrome. `main.cpp` launches `RetouchWindow`; `MainWindow` is deleted.

**Tech Stack:** C++17, Qt Widgets (`QMainWindow`, `QStackedWidget`, `QToolBar`, `QDockWidget`, `QAction`), CMake, libgphoto2, libraw.

## Global Constraints

- Framework: Qt Widgets only (no QML). Match existing code style (2-space indent, `m_` member prefix, programmatic UI in `buildXxx()` helpers).
- Build: `cmake -S /home/janel/Development/imgcapture -B /home/janel/Development/imgcapture/build && cmake --build /home/janel/Development/imgcapture/build -j`
- This codebase has **no unit-test framework for the UI layer**. Verification per task = clean compile + link, the existing `--undotest` regression still passes, and a manual behavioral checklist. Do NOT invent a fake test harness.
- `--undotest <raw>` and `--export-icon <png>` CLI modes must keep working.
- App/window title stays "NikonTether".
- Camera connection and live view must survive mode switches (only visibility changes; `TetherView` is never destroyed).

---

### Task 1: Extract `TetherView` from `MainWindow`

Create a self-contained tether widget. `MainWindow` stays in place and untouched this task, so the build stays green; it is removed in Task 4.

**Files:**
- Create: `src/ui/TetherView.h`
- Create: `src/ui/TetherView.cpp`
- Modify: `CMakeLists.txt` (add `src/ui/TetherView.cpp` to the `add_executable` list, right after `src/ui/MainWindow.cpp`)

**Interfaces:**
- Consumes: `CameraController` (`src/camera/CameraController.h`), `LiveViewWidget`, `ControlsPanel`, `PreviewWindow`, `SessionManager`, `NefPreview` — same collaborators `MainWindow` uses today.
- Produces (relied on by Tasks 2–3):
  - `class TetherView : public QWidget`
  - `explicit TetherView(QWidget *parent = nullptr)`
  - `ControlsPanel *controlsPanel() const`
  - `QList<QAction *> tetherActions() const`
  - `void setActive(bool active)`
  - signals `void captureComplete(const QString &path)` and `void statusMessage(const QString &message)`

- [ ] **Step 1: Create `src/ui/TetherView.h`**

```cpp
#pragma once

#include <QWidget>
#include <QList>

#include "capture/SessionManager.h"
#include "camera/CameraSettings.h"

class CameraController;
class LiveViewWidget;
class ControlsPanel;
class PreviewWindow;
class QAction;
class QTabWidget;
class QShortcut;

// Self-contained tethering view: owns the camera pipeline (controller, session),
// the live-view / preview tabs, the camera ControlsPanel, and the tether actions
// (Connect / Disconnect / Live View / Capture / New Session). Embed it as a page
// in a host window; the host places controlsPanel() in a dock and tetherActions()
// on a toolbar, and listens for captureComplete()/statusMessage().
class TetherView : public QWidget {
    Q_OBJECT
public:
    explicit TetherView(QWidget *parent = nullptr);

    // Camera ControlsPanel widget, for the host to place in a dock.
    ControlsPanel *controlsPanel() const { return m_controls; }
    // Tether actions in display order, for the host toolbar.
    QList<QAction *> tetherActions() const;

    // Enable/disable the app-wide spacebar capture shortcut. The host calls
    // setActive(true) when the tether page is shown, false otherwise, so Space
    // only captures while tethering.
    void setActive(bool active);

signals:
    void captureComplete(const QString &path);
    void statusMessage(const QString &message);

private slots:
    void onConnect();
    void onDisconnect();
    void onToggleLiveView(bool on);
    void onNewSession();
    void handleConnected(const QString &name, const ConfigOptionMap &options);
    void handleDisconnected();
    void handleCaptureComplete(const QString &path);
    void handleError(const QString &message);

private:
    void buildUi();
    void setConnectedState(bool connected);
    void updateCaptureShortcut();

    CameraController *m_controller = nullptr;
    SessionManager m_session;

    QTabWidget *m_viewTabs = nullptr;
    LiveViewWidget *m_liveView = nullptr;
    ControlsPanel *m_controls = nullptr;
    PreviewWindow *m_preview = nullptr;

    QAction *m_connectAction = nullptr;
    QAction *m_disconnectAction = nullptr;
    QAction *m_liveViewAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_sessionAction = nullptr;
    QShortcut *m_captureShortcut = nullptr;
    bool m_active = false;
};
```

- [ ] **Step 2: Create `src/ui/TetherView.cpp`**

This is `MainWindow`'s logic minus the window chrome (no menu bar, no status bar, no filmstrip, no owned RetouchWindow). Status text is emitted via `statusMessage()`; captures are emitted via `captureComplete()`; the ControlsPanel and actions are exposed for the host.

```cpp
#include "ui/TetherView.h"

#include "camera/CameraController.h"
#include "ui/LiveViewWidget.h"
#include "ui/ControlsPanel.h"
#include "ui/PreviewWindow.h"
#include "capture/NefPreview.h"

#include <QAction>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>

TetherView::TetherView(QWidget *parent) : QWidget(parent) {
    m_controller = new CameraController(this);
    buildUi();

    connect(m_controller, &CameraController::connected, this, &TetherView::handleConnected);
    connect(m_controller, &CameraController::disconnected, this, &TetherView::handleDisconnected);
    connect(m_controller, &CameraController::liveFrame,
            m_liveView, &LiveViewWidget::setFrame);
    connect(m_controller, &CameraController::captureComplete,
            this, &TetherView::handleCaptureComplete);
    connect(m_controller, &CameraController::captureStarted, this,
            [this] { emit statusMessage("Capturing…"); });
    connect(m_controller, &CameraController::configChanged, this,
            [this](const QString &w, const QString &v) {
                m_controls->updateValue(w, v);
                emit statusMessage(QString("Set %1 = %2").arg(w, v));
            });
    connect(m_controller, &CameraController::cameraError, this, &TetherView::handleError);
    connect(m_controller, &CameraController::log, this,
            [this](const QString &m) { emit statusMessage(m); });

    // Controls -> controller
    connect(m_controls, &ControlsPanel::configEditRequested,
            m_controller, &CameraController::setConfig);
    connect(m_controls, &ControlsPanel::autofocusRequested,
            m_controller, &CameraController::triggerAutofocus);
    connect(m_controls, &ControlsPanel::captureRequested,
            m_controller, &CameraController::capture);
    connect(m_liveView, &LiveViewWidget::focusRequested,
            m_controller, &CameraController::setAfArea);

    // Start with a default session so captures always have a home.
    m_controller->setSaveDirectory(m_session.startSession("studio"));
    setConnectedState(false);
}

void TetherView::buildUi() {
    // Actions are created here but placed on the HOST toolbar via tetherActions().
    m_connectAction = new QAction("Connect", this);
    m_disconnectAction = new QAction("Disconnect", this);
    m_liveViewAction = new QAction("Live View", this);
    m_liveViewAction->setCheckable(true);
    m_captureAction = new QAction("Capture (Space)", this);
    m_sessionAction = new QAction("New Session…", this);

    connect(m_connectAction, &QAction::triggered, this, &TetherView::onConnect);
    connect(m_disconnectAction, &QAction::triggered, this, &TetherView::onDisconnect);
    connect(m_liveViewAction, &QAction::toggled, this, &TetherView::onToggleLiveView);
    connect(m_captureAction, &QAction::triggered, m_controller, &CameraController::capture);
    connect(m_sessionAction, &QAction::triggered, this, &TetherView::onNewSession);

    // Spacebar triggers a capture while tethering — except on the Preview tab,
    // where Space is reserved for pan, and only while this view is active.
    m_captureShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_captureShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_captureShortcut, &QShortcut::activated,
            m_controller, &CameraController::capture);

    // Center: tabbed Live View / Preview fills the widget.
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_viewTabs = new QTabWidget;
    m_liveView = new LiveViewWidget;
    m_preview = new PreviewWindow;
    m_viewTabs->addTab(m_liveView, "Live View");
    m_viewTabs->addTab(m_preview, "Preview");
    vbox->addWidget(m_viewTabs, 1);

    connect(m_viewTabs, &QTabWidget::currentChanged, this, [this](int) {
        updateCaptureShortcut();
        if (m_viewTabs->currentWidget() == m_preview) m_preview->focusView();
    });

    m_controls = new ControlsPanel; // parented into the host's dock later
    updateCaptureShortcut();
}

QList<QAction *> TetherView::tetherActions() const {
    return { m_connectAction, m_disconnectAction, m_liveViewAction,
             m_captureAction, m_sessionAction };
}

void TetherView::setActive(bool active) {
    m_active = active;
    updateCaptureShortcut();
}

void TetherView::updateCaptureShortcut() {
    // Capture on Space only while tethering and not on the Preview tab.
    bool onPreview = m_viewTabs && m_viewTabs->currentWidget() == m_preview;
    if (m_captureShortcut) m_captureShortcut->setEnabled(m_active && !onPreview);
}

void TetherView::onConnect() {
    emit statusMessage("Connecting…");
    m_controller->connectCamera();
}

void TetherView::onDisconnect() {
    if (m_liveViewAction->isChecked()) m_liveViewAction->setChecked(false);
    m_controller->disconnectCamera();
}

void TetherView::onToggleLiveView(bool on) {
    if (on) m_controller->startLiveView();
    else { m_controller->stopLiveView(); m_liveView->clearFrame(); }
}

void TetherView::onNewSession() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Session",
                                         "Session name:", QLineEdit::Normal,
                                         "studio", &ok);
    if (!ok) return;
    QString dir = m_session.startSession(name);
    m_controller->setSaveDirectory(dir);
    emit statusMessage("Session: " + dir);
}

void TetherView::handleConnected(const QString &name, const ConfigOptionMap &options) {
    m_controls->populate(options);
    setConnectedState(true);
    emit statusMessage("Connected: " + name);
}

void TetherView::handleDisconnected() {
    setConnectedState(false);
    m_liveView->clearFrame();
    emit statusMessage("Disconnected");
}

void TetherView::handleCaptureComplete(const QString &path) {
    QImage preview = NefPreview::extract(path);
    // Notify the host so it can add the capture to the shared filmstrip, even if
    // no preview could be extracted (host draws a placeholder then).
    emit captureComplete(path);
    if (!preview.isNull()) {
        m_preview->showImage(path, preview);
        m_viewTabs->setCurrentWidget(m_preview);
        emit statusMessage("Captured: " + path);
    } else {
        emit statusMessage("Captured (no embedded preview): " + path);
    }
}

void TetherView::handleError(const QString &message) {
    emit statusMessage("Error: " + message);
    QMessageBox::warning(this, "Camera Error", message);
}

void TetherView::setConnectedState(bool connected) {
    m_connectAction->setEnabled(!connected);
    m_disconnectAction->setEnabled(connected);
    m_liveViewAction->setEnabled(connected);
    m_captureAction->setEnabled(connected);
    m_controls->setEnabledControls(connected);
}
```

- [ ] **Step 3: Add `TetherView.cpp` to the build**

In `CMakeLists.txt`, in the `add_executable(nikontether ...)` source list, add the line immediately after `src/ui/MainWindow.cpp`:

```cmake
    src/ui/MainWindow.cpp
    src/ui/TetherView.cpp
```

- [ ] **Step 4: Build and verify it compiles/links**

Run: `cmake -S /home/janel/Development/imgcapture -B /home/janel/Development/imgcapture/build && cmake --build /home/janel/Development/imgcapture/build -j`
Expected: builds cleanly, no errors. (`TetherView` is compiled but not yet referenced anywhere — that's fine.)

- [ ] **Step 5: Commit**

```bash
git add src/ui/TetherView.h src/ui/TetherView.cpp CMakeLists.txt
git commit -m "Extract self-contained TetherView widget from MainWindow"
```

---

### Task 2: Host `TetherView` inside `RetouchWindow`'s central stack

Add the central `QStackedWidget` with the editing tabs (page 0) and a `TetherView` (page 1), move the filmstrip below the stack so it shows in both modes, add the Controls dock and tether toolbar, and wire capture → filmstrip and tether status → status bar. **No mode-switch buttons yet** — the window stays on the Retouch page; this task proves the tether page constructs and embeds cleanly. Mode UI comes in Task 3.

**Files:**
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes (from Task 1): `TetherView`, `controlsPanel()`, `tetherActions()`, `captureComplete()`, `statusMessage()`.
- Produces (relied on by Task 3): members `m_tetherView`, `m_modeStack`, `m_controlsDock`, `m_tetherToolBar`, `m_saveAction`, `m_saveAllAction`, `m_exportAction`.

- [ ] **Step 1: Declare new members and forward-declares in `src/edit/RetouchWindow.h`**

Add the forward declaration near the other `class ...;` lines (after `class QListWidget;`):

```cpp
class TetherView;
```

Add these members inside the `private:` section (place near `m_tabs` / `m_filmstrip`):

```cpp
    // Unified window: central stack swaps editing tabs (page 0) / tether (page 1).
    QStackedWidget *m_modeStack = nullptr;
    TetherView *m_tetherView = nullptr;
    QDockWidget *m_controlsDock = nullptr; // camera controls, shown in Tether mode
    QToolBar *m_tetherToolBar = nullptr;   // Connect/Disconnect/LiveView/Capture/…

    // Promoted from constructor locals so mode chrome can enable/disable them.
    QAction *m_saveAction = nullptr;
    QAction *m_saveAllAction = nullptr;
    QAction *m_exportAction = nullptr;
```

- [ ] **Step 2: Include `TetherView` in `src/edit/RetouchWindow.cpp`**

Add near the top includes (after `#include "ui/FilmstripWidget.h"`):

```cpp
#include "ui/TetherView.h"
```

- [ ] **Step 3: Promote Save/Save All/Export actions to members**

In the constructor, change the three local action declarations (currently `QAction *saveAction`, `saveAllAction`, `exportAction`) to assign the new members instead. Replace:

```cpp
    QAction *saveAction = toolbar->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    QAction *saveAllAction = toolbar->addAction("Save All");
    toolbar->addSeparator();
    QAction *exportAction = toolbar->addAction("Export…");
```

with:

```cpp
    m_saveAction = toolbar->addAction("Save");
    m_saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    m_saveAllAction = toolbar->addAction("Save All");
    toolbar->addSeparator();
    m_exportAction = toolbar->addAction("Export…");
```

Then update the three `connect(...)` lines that reference the old locals to use the members:

```cpp
    connect(m_saveAction, &QAction::triggered, this, &RetouchWindow::onSave);
    connect(m_saveAllAction, &QAction::triggered, this, &RetouchWindow::onSaveAll);
    connect(m_exportAction, &QAction::triggered, this, &RetouchWindow::onExport);
```

And in the File-menu block, replace `fileMenu->addAction(saveAction);` / `saveAllAction` / `exportAction` with `m_saveAction` / `m_saveAllAction` / `m_exportAction`.

- [ ] **Step 4: Restructure the central widget to a stack + shared filmstrip**

Replace the current central-widget block (the lines building `central`, `m_tabs`, `m_filmstrip`, and `setCentralWidget`) with:

```cpp
    // Center: a stack (editing tabs / tether) with the shared filmstrip below,
    // so the filmstrip is visible in both modes.
    auto *central = new QWidget;
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &RetouchWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            &RetouchWindow::onTabCloseRequested);

    m_tetherView = new TetherView;

    m_modeStack = new QStackedWidget;
    m_modeStack->addWidget(m_tabs);       // index 0 = Retouch
    m_modeStack->addWidget(m_tetherView); // index 1 = Tether

    m_filmstrip = new FilmstripWidget;
    connect(m_filmstrip, &FilmstripWidget::frameSelected, this,
            &RetouchWindow::onFilmstripSelected);

    vbox->addWidget(m_modeStack, 1);
    vbox->addWidget(m_filmstrip, 0);
    setCentralWidget(central);
```

- [ ] **Step 5: Build the Controls dock and tether toolbar; wire tether signals**

Add this block in the constructor after `buildViewMenu();` and before the `m_statusLabel` creation:

```cpp
    // Tether chrome: camera controls dock + tether action toolbar. Visibility is
    // driven by mode in Task 3; created hidden here.
    m_controlsDock = new QDockWidget("Controls", this);
    m_controlsDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_controlsDock->setWidget(m_tetherView->controlsPanel());
    addDockWidget(Qt::RightDockWidgetArea, m_controlsDock);
    m_controlsDock->hide();

    m_tetherToolBar = addToolBar("Tether");
    m_tetherToolBar->setMovable(false);
    m_tetherToolBar->addActions(m_tetherView->tetherActions());
    m_tetherToolBar->hide();

    // Captures flow into the shared filmstrip; tether status into the status bar.
    connect(m_tetherView, &TetherView::captureComplete, this,
            [this](const QString &path) { addToFilmstrip(path); });
    connect(m_tetherView, &TetherView::statusMessage, this,
            [this](const QString &msg) { m_statusLabel->setText(msg); });
```

Note: `m_statusLabel` is created just after this block, so the lambdas capture it by `this` and only fire later at runtime — safe. Keep the existing `m_statusLabel = new QLabel(...)` line right after.

- [ ] **Step 6: Build and verify**

Run: `cmake --build /home/janel/Development/imgcapture/build -j`
Expected: builds cleanly.

- [ ] **Step 7: Regression — `--undotest` still constructs the (now heavier) window**

Run (offscreen so it needs no display; supply any RAW file you have — substitute the path):
`QT_QPA_PLATFORM=offscreen /home/janel/Development/imgcapture/build/nikontether --undotest /path/to/sample.nef`
Expected: prints the undo/redo trace and exits 0, exactly as before. This proves `RetouchWindow` (now creating a `TetherView`, which creates a `CameraController`) constructs headlessly without a camera. If you have no sample RAW, at minimum confirm the build succeeded in Step 6.

- [ ] **Step 8: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Embed TetherView in RetouchWindow central stack with shared filmstrip"
```

---

### Task 3: Add mode-switch buttons, per-mode chrome, and capture→edit wiring

Add the two mutually-exclusive **Tether | Retouch** buttons at the left of the Main toolbar, a `setMode()` that swaps the stack page and shows/hides the right chrome, default to Retouch mode on startup, and make clicking a filmstrip thumbnail switch to Retouch mode with the photo open.

**Files:**
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes (from Task 2): `m_modeStack`, `m_tetherView`, `m_controlsDock`, `m_tetherToolBar`, `m_saveAction`, `m_saveAllAction`, `m_exportAction`.
- Produces: `enum class Mode`, `void setMode(Mode)`, `void applyModeChrome(Mode)`, actions `m_tetherModeAction`, `m_retouchModeAction`.

- [ ] **Step 1: Declare the mode API in `src/edit/RetouchWindow.h`**

In the `public:` section add:

```cpp
    enum class Mode { Retouch, Tether };
    void setMode(Mode mode);
```

In the `private:` section add:

```cpp
    void applyModeChrome(Mode mode);
    QAction *m_tetherModeAction = nullptr;
    QAction *m_retouchModeAction = nullptr;
```

- [ ] **Step 2: Add mode buttons at the left of the Main toolbar**

In the constructor, immediately after `toolbar->setMovable(false);` and **before** the `m_saveAction = toolbar->addAction("Save");` line, insert:

```cpp
    // Mode switch: mutually-exclusive Tether / Retouch at the far left.
    m_retouchModeAction = toolbar->addAction("Retouch");
    m_tetherModeAction = toolbar->addAction("Tether");
    m_retouchModeAction->setCheckable(true);
    m_tetherModeAction->setCheckable(true);
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(m_retouchModeAction);
    modeGroup->addAction(m_tetherModeAction);
    connect(m_retouchModeAction, &QAction::triggered, this,
            [this] { setMode(Mode::Retouch); });
    connect(m_tetherModeAction, &QAction::triggered, this,
            [this] { setMode(Mode::Tether); });
    toolbar->addSeparator();
```

Add `#include <QActionGroup>` to the includes at the top of the file (near the other `#include <QAction>`).

- [ ] **Step 3: Implement `setMode` and `applyModeChrome`**

Add these two methods near `RetouchWindow::onFilmstripSelected` in the .cpp:

```cpp
void RetouchWindow::setMode(Mode mode) {
    m_modeStack->setCurrentWidget(mode == Mode::Tether
                                      ? static_cast<QWidget *>(m_tetherView)
                                      : static_cast<QWidget *>(m_tabs));
    applyModeChrome(mode);
    // Keep the toolbar buttons in sync when called programmatically.
    QSignalBlocker b1(m_tetherModeAction);
    QSignalBlocker b2(m_retouchModeAction);
    m_tetherModeAction->setChecked(mode == Mode::Tether);
    m_retouchModeAction->setChecked(mode == Mode::Retouch);
}

void RetouchWindow::applyModeChrome(Mode mode) {
    const bool tether = (mode == Mode::Tether);

    // Tether chrome.
    if (m_tetherToolBar) m_tetherToolBar->setVisible(tether);
    if (m_controlsDock)  m_controlsDock->setVisible(tether);
    if (m_tetherView)    m_tetherView->setActive(tether);

    // Editing chrome.
    if (tether) deselectAllTools(); // exit any active tool + hide the options row
    if (m_toolsBar)        m_toolsBar->setVisible(!tether);
    if (m_adjustmentsDock) m_adjustmentsDock->setVisible(!tether);
    if (m_historyDock)     m_historyDock->setVisible(!tether);

    // Editing-only actions are meaningless while tethering.
    m_saveAction->setEnabled(!tether);
    m_saveAllAction->setEnabled(!tether);
    m_exportAction->setEnabled(!tether);
    if (tether) {
        m_undoAction->setEnabled(false);
        m_redoAction->setEnabled(false);
    } else {
        // Restore undo/redo + dock state for the current tab.
        onTabChanged(m_tabs->currentIndex());
    }
}
```

- [ ] **Step 4: Switch to Retouch mode on a filmstrip click**

In `onFilmstripSelected`, add the mode switch so clicking a thumbnail (from either mode) jumps into editing:

```cpp
void RetouchWindow::onFilmstripSelected(const QString &path) {
    setMode(Mode::Retouch);
    openPhoto(path);
}
```

Also wire the filmstrip's context-menu "Retouch" action for parity. In the constructor, right after the existing `connect(m_filmstrip, &FilmstripWidget::frameSelected, ...)`, add:

```cpp
    connect(m_filmstrip, &FilmstripWidget::retouchRequested, this,
            &RetouchWindow::onFilmstripSelected);
```

- [ ] **Step 5: Default to Retouch mode at the end of the constructor**

As the last line of the `RetouchWindow` constructor (after `setDockEnabled(false);` and the Esc-shortcut block), add:

```cpp
    setMode(Mode::Retouch);
```

- [ ] **Step 6: Build and verify**

Run: `cmake --build /home/janel/Development/imgcapture/build -j`
Expected: builds cleanly.

- [ ] **Step 7: Manual behavioral check**

Run the app normally: `/home/janel/Development/imgcapture/build/nikontether`
Verify:
- Opens in **Retouch** mode (Retouch button active; editing tools bar/adjustments dock visible; tether toolbar + Controls dock hidden).
- Click **Tether** → live view + Preview tabs shown; Controls dock + Connect/Disconnect/Live View/Capture/New Session toolbar appear; editing tools bar / adjustments / history hidden; Save/Save All/Export disabled.
- Click **Retouch** → editing chrome returns; tether chrome hidden.
- With a camera connected in Tether mode, capturing (button and Space) adds a thumbnail to the filmstrip and stays in Tether. (If no camera, skip capture; still confirm mode chrome.)
- Click any filmstrip thumbnail → switches to Retouch mode with that photo open in a tab.

- [ ] **Step 8: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Add Tether/Retouch mode switch with per-mode chrome and capture-to-edit"
```

---

### Task 4: Retire `MainWindow` and make `RetouchWindow` the sole window

Point `main.cpp` at `RetouchWindow`, delete `MainWindow`, drop it from the build, and give the window the app title.

**Files:**
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt` (remove `src/ui/MainWindow.cpp`)
- Delete: `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`
- Modify: `src/edit/RetouchWindow.cpp` (window title)

**Interfaces:**
- Consumes: `RetouchWindow` (already the `--undotest` entry point).

- [ ] **Step 1: Launch `RetouchWindow` from `main.cpp`**

In `src/main.cpp`, remove the include:

```cpp
#include "ui/MainWindow.h"
```

Replace the normal run path at the bottom of `main`:

```cpp
    MainWindow window;
    window.show();
    return app.exec();
```

with:

```cpp
    RetouchWindow window;
    window.show();
    return app.exec();
```

(`#include "edit/RetouchWindow.h"` is already present.)

- [ ] **Step 2: Give the window the app title**

In `src/edit/RetouchWindow.cpp`, change the constructor's title line from:

```cpp
    setWindowTitle("Retouch");
```

to:

```cpp
    setWindowTitle("NikonTether");
```

- [ ] **Step 3: Remove `MainWindow` from the build and delete its files**

In `CMakeLists.txt`, delete the line `    src/ui/MainWindow.cpp` from the `add_executable` list (leave `src/ui/TetherView.cpp`).

```bash
git rm src/ui/MainWindow.h src/ui/MainWindow.cpp
```

- [ ] **Step 4: Build and verify no dangling references**

Run: `cmake -S /home/janel/Development/imgcapture -B /home/janel/Development/imgcapture/build && cmake --build /home/janel/Development/imgcapture/build -j`
Expected: builds cleanly with no reference to `MainWindow`. (Reconfigure is needed because a source file was removed.)

- [ ] **Step 5: Verify all three run modes**

- Normal: `/home/janel/Development/imgcapture/build/nikontether` → single window titled "NikonTether", opens in Retouch mode, Tether/Retouch switching works.
- Undo regression: `QT_QPA_PLATFORM=offscreen /home/janel/Development/imgcapture/build/nikontether --undotest /path/to/sample.nef` → prints trace, exits 0.
- Icon export: `/home/janel/Development/imgcapture/build/nikontether --export-icon /tmp/icon.png` → writes the PNG, exits 0.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Retire MainWindow; launch unified RetouchWindow as sole window"
```

---

## Self-Review Notes

- **Spec coverage:** one window (Task 4) ✓; mode switch as full views via `QStackedWidget` (Tasks 2–3) ✓; startup Retouch (Task 3 Step 5) ✓; two mutually-exclusive toolbar buttons at toolbar-left (Task 3 Step 2) ✓; per-mode chrome incl. Controls dock + editing docks/bars + editing-only actions disabled (Task 3 Step 3) ✓; shared filmstrip below both modes (Task 2 Step 4) ✓; capture stays in Tether + appends to filmstrip (Task 1 `handleCaptureComplete` → Task 2 Step 5 wiring) ✓; click thumbnail → Retouch mode with photo open (Task 3 Step 4) ✓; `TetherView` self-contained component (Task 1) ✓; retire `MainWindow`, update `main.cpp`/CMake, keep `--undotest`/`--export-icon`, app title (Task 4) ✓.
- **Testing reality:** No UI unit-test framework exists here; gates are compile+link, the `--undotest`/`--export-icon` CLI regressions, and a manual behavioral checklist — stated honestly rather than faked.
- **Known minor limitation (acceptable for v1, per spec Out of Scope):** switching to Tether and back forces the Adjustments/History docks visible even if the user had manually hidden them in Retouch mode. No "remember last mode" persistence — startup is always Retouch.
