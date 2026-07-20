# Photo Background Layer & File > New Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make an opened photo appear as a locked "Background" row at the bottom of the Layers panel (duplicate-only, no erase/heal), and add a File > New action that creates a blank, fully-editable canvas in an "Untitled" tab that prompts for a save location on first save.

**Architecture:** No new persisted data fields. The Background row is a synthetic, non-`Mask` entry that `LayersPanel` renders whenever the current tab has a real file path; selecting it reuses the existing `activeMaskIndex() == -1` ("no mask selected") state that `RetouchTab` already supports. Duplicating it calls the existing `RetouchTab::addImageLayer()` (used today for image-layer drag/drop) with the tab's own path, then moves the new layer to the bottom of the stack. File > New creates a `RetouchTab` that skips `RawLoader`/`EditSidecar::load` and starts from a blank transparent `QImage`; it's tracked in `m_openTabs` under a synthetic key until the user's first Save assigns it a real path via a new save-location dialog.

**Tech Stack:** C++17, Qt6 Widgets, existing `QtConcurrent`/`QFutureWatcher` async-decode pattern, CTest-registered standalone test executables (see `tests/AdjustmentsPaintTest.cpp` for the house style: a `main()` with `assert()`, built with `-UNDEBUG` since the project is Release/NDEBUG by default).

## Global Constraints

- Tone/color/curves/levels adjustment panels must keep editing the base `Adjustments` exactly as they do today, on every tab, regardless of Background-lock state — do not gate those panels.
- No `EditSidecar` format/version change. No new fields on `Mask`/`Adjustments`.
- Untitled tabs must never be added to the filmstrip and must never be touched by `loadSession()`/session scanning.
- Every new UI action must follow the existing signal/slot pattern already used in `LayersPanel`/`RetouchWindow` (emit intent signals from the view, handle them in `RetouchWindow`) — do not have `LayersPanel` reach into `RetouchTab` directly.

---

### Task 1: Background pseudo-row in LayersPanel

**Files:**
- Modify: `src/ui/LayersPanel.h`
- Modify: `src/ui/LayersPanel.cpp`

**Interfaces:**
- Consumes: existing `QVector<Mask> m_masks`, `int m_active` (already `-1` when nothing is selected), existing `m_maskList` (`QListWidget`), existing signals `selectMaskRequested(int)`, `duplicateMaskRequested()`, `maskReorderRequested(int,int)`.
- Produces: `void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex, bool hasBackground)` (signature change — old 2-arg call sites must be updated in Task 2), so later tasks/RetouchWindow know the new parameter name and position.

- [ ] **Step 1: Change `setMasks` signature and store the flag**

In `src/ui/LayersPanel.h`, change:
```cpp
    void setMasks(const QVector<Mask> &masks, int activeIndex);
```
to:
```cpp
    void setMasks(const QVector<Mask> &masks, int activeIndex, bool hasBackground);
```
and add a private member near `m_active`:
```cpp
    bool m_hasBackground = false;
```

In `src/ui/LayersPanel.cpp`, update the definition:
```cpp
void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex, bool hasBackground) {
    m_masks = masks;
    m_active = activeIndex;
    m_hasBackground = hasBackground;
    setEnabled(true);
    rebuildList();
    loadActive();
}
```

- [ ] **Step 2: Prepend the Background row in `rebuildList()`**

Replace the body of `LayersPanel::rebuildList()`:
```cpp
void LayersPanel::rebuildList() {
    m_syncing = true;
    m_maskList->clear();
    if (m_hasBackground) {
        auto *bg = new QListWidgetItem(QStringLiteral("Background \xF0\x9F\x94\x92")); // trailing lock emoji
        bg->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled); // no drag, no checkbox
        m_maskList->addItem(bg);
    }
    for (int i = 0; i < m_masks.size(); ++i) {
        const Mask &m = m_masks[i];
        QString label = m.name.isEmpty()
                            ? QString("Layer %1 (%2)").arg(i + 1).arg(maskTypeLabel(m.type))
                            : m.name;
        if (m.isImageLayer()) {
            if (m.sourceMissing) label += " (missing)";
            else if (m.sourceImageCache.isNull()) label += " (loading…)";
        }
        auto *item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
        m_maskList->addItem(item);
    }
    const int row = m_hasBackground ? m_active + 1 : m_active;
    if (row >= 0 && row < m_maskList->count())
        m_maskList->setCurrentRow(row);
    else if (m_hasBackground && m_active == -1)
        m_maskList->setCurrentRow(0);
    m_syncing = false;
}
```

- [ ] **Step 3: Map list rows back to mask indices (offset by the Background row)**

Replace the three `m_maskList`/`rowsMoved` connections made in the constructor:
```cpp
    connect(m_maskList, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!m_syncing) emit selectMaskRequested(m_hasBackground ? i - 1 : i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                if (m_hasBackground && i == 0) return; // Background has no visibility toggle
                emit maskVisibleChanged(m_hasBackground ? i - 1 : i,
                                        item->checkState() == Qt::Checked);
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &,
                   int destRow) {
                if (m_syncing) return;
                if (m_hasBackground && (start == 0 || destRow == 0)) return; // can't move Background
                int to = destRow > start ? destRow - 1 : destRow;
                int off = m_hasBackground ? 1 : 0;
                emit maskReorderRequested(start - off, to - off);
            });
```
Since the Background item's flags (Step 2) don't include `Qt::ItemIsDragEnabled`/`ItemIsDropEnabled`, Qt won't let the user drag it or drop onto row 0 in the first place — the `start == 0 || destRow == 0` check is a defensive backstop.

- [ ] **Step 4: Enable Duplicate (but not Delete/name/opacity/blend) when the Background row is selected**

In `LayersPanel::loadActive()`, find:
```cpp
    const bool has = m_active >= 0 && m_active < m_masks.size();
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_duplicate, m_delete, m_levelsPanel})
        w->setEnabled(has);
```
Change to:
```cpp
    const bool has = m_active >= 0 && m_active < m_masks.size();
    const bool isBackground = m_hasBackground && m_active == -1;
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_delete, m_levelsPanel})
        w->setEnabled(has);
    m_duplicate->setEnabled(has || isBackground);
```
(leave the rest of the `if (has) { ... }` block below untouched — it only runs for real masks, which is correct: Background has no name/opacity/blend fields to populate.)

- [ ] **Step 5: Build and fix call sites**

Run:
```bash
cmake --build build 2>&1 | grep -A2 "error:" | head -60
```
Expected: a compile error in `RetouchWindow.cpp` at every old 2-arg `setMasks(...)` call and at `clear()` if it calls the old signature — these are fixed in Task 2. Confirm the *only* errors are in `RetouchWindow.cpp` (i.e. `LayersPanel.cpp`/`.h` themselves compile clean) before moving on.

- [ ] **Step 6: Commit**

```bash
git add src/ui/LayersPanel.h src/ui/LayersPanel.cpp
git commit -m "Add locked Background pseudo-row to LayersPanel"
```

---

### Task 2: Wire Background selection, duplicate, and tool gating in RetouchWindow

**Files:**
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `LayersPanel::setMasks(masks, activeIndex, hasBackground)` from Task 1; `RetouchTab::addImageLayer(const QString &path)` (existing, returns new index, appends to end of `masks`); `RetouchTab::moveMask(int from, int to)` (existing); `RetouchTab::path()`, `RetouchTab::activeMaskIndex()`, `RetouchTab::masks()` (existing).
- Produces: nothing new consumed by later tasks (this task is UI wiring only), but establishes the pattern Task 5/6 follow for tab-state checks.

- [ ] **Step 1: Update `refreshMaskPanel()` to pass `hasBackground` and gate heal too**

Find (`RetouchWindow.cpp`, current `refreshMaskPanel()`):
```cpp
void RetouchWindow::refreshMaskPanel() {
    RetouchTab *tab = currentTab();
    const bool ready = tab && tab->isReady();
    if (m_layersPanel) {
        if (ready) m_layersPanel->setMasks(tab->masks(), tab->activeMaskIndex());
        else m_layersPanel->clear();
    }
    if (m_eraseToggle) {
        const int idx = ready ? tab->activeMaskIndex() : -1;
        const bool isImageLayer = ready && idx >= 0 && idx < tab->masks().size() &&
                                  tab->masks()[idx].isImageLayer();
        m_eraseToggle->setEnabled(isImageLayer);
        if (!isImageLayer && m_eraseToggle->isChecked())
            m_eraseToggle->setChecked(false);
    }
}
```
Replace with:
```cpp
void RetouchWindow::refreshMaskPanel() {
    RetouchTab *tab = currentTab();
    const bool ready = tab && tab->isReady();
    if (m_layersPanel) {
        if (ready) m_layersPanel->setMasks(tab->masks(), tab->activeMaskIndex(),
                                           !tab->path().isEmpty());
        else m_layersPanel->clear();
    }
    const int idx = ready ? tab->activeMaskIndex() : -1;
    const bool isImageLayer = ready && idx >= 0 && idx < tab->masks().size() &&
                              tab->masks()[idx].isImageLayer();
    if (m_eraseToggle) {
        m_eraseToggle->setEnabled(isImageLayer);
        if (!isImageLayer && m_eraseToggle->isChecked())
            m_eraseToggle->setChecked(false);
    }
    if (m_healToggle) {
        m_healToggle->setEnabled(isImageLayer);
        if (!isImageLayer && m_healToggle->isChecked())
            m_healToggle->setChecked(false);
    }
}
```
Note this makes heal require an image layer to be selected at all (previously it had no gating whatsoever — this is the intended behavior change from the spec: heal is blocked on the Background row *and* on non-image masks, matching how erase already behaves).

- [ ] **Step 2: Make Duplicate create an image-layer duplicate when Background is selected**

Find:
```cpp
    connect(m_layersPanel, &LayersPanel::duplicateMaskRequested, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) { tab->duplicateActiveMask(); refreshMaskPanel(); }
    });
```
Replace with:
```cpp
    connect(m_layersPanel, &LayersPanel::duplicateMaskRequested, this, [this] {
        RetouchTab *tab = currentTab();
        if (!tab) return;
        if (tab->activeMaskIndex() == -1 && !tab->path().isEmpty()) {
            // Duplicating the locked Background row: add a real, unlocked
            // image layer sourced from the same photo, placed at the
            // bottom of the stack (directly above Background).
            int idx = tab->addImageLayer(tab->path());
            tab->moveMask(idx, 0);
            tab->selectMask(0);
        } else {
            tab->duplicateActiveMask();
        }
        refreshMaskPanel();
    });
```

- [ ] **Step 3: Build**

```bash
cmake --build build 2>&1 | tail -40
```
Expected: clean build, no errors.

- [ ] **Step 4: Manual verification**

Run the app, open a photo:
- Confirm the Layers panel shows a "Background 🔒" row pinned at the top of the list (bottom of the visual stack ordering used elsewhere in this app — verify against how existing mask rows are ordered top-to-bottom vs stack order, and adjust the Background row's position in Step 2 of Task 1 if the existing convention is reversed).
- Confirm dragging the Background row does nothing, and Delete is disabled while it's selected.
- Confirm Erase and Heal toolbar buttons are disabled while Background is selected.
- Click Duplicate: confirm a new, unlocked image layer appears; select it and confirm Erase/Heal become enabled and work on it.
- Confirm tone/color/curves/levels panels still edit the same way they did before this change, regardless of which row is selected.

- [ ] **Step 5: Commit**

```bash
git add src/edit/RetouchWindow.cpp
git commit -m "Gate erase/heal on Background row; wire Background duplicate"
```

---

### Task 3: Pixel-size helper for the New Document dialog (unit-tested)

**Files:**
- Create: `src/edit/NewDocumentSize.h`
- Create: `src/edit/NewDocumentSize.cpp`
- Test: `tests/NewDocumentSizeTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `enum class SizeUnit { Pixels, Inches, Centimeters };` and
  `QSize computeCanvasPixelSize(double width, double height, SizeUnit unit, double dpi);`
  — Task 4's dialog and Task 5's tab-creation call this exact function.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/NewDocumentSizeTest.cpp
#include "edit/NewDocumentSize.h"
#include <cassert>

int main() {
    // Pixels: passes through unchanged (dpi ignored).
    {
        QSize s = computeCanvasPixelSize(800, 600, SizeUnit::Pixels, 300);
        assert(s.width() == 800 && s.height() == 600);
    }
    // Inches at 300 DPI.
    {
        QSize s = computeCanvasPixelSize(4, 6, SizeUnit::Inches, 300);
        assert(s.width() == 1200 && s.height() == 1800);
    }
    // Centimeters at 96 DPI (1 inch = 2.54 cm).
    {
        QSize s = computeCanvasPixelSize(2.54, 5.08, SizeUnit::Centimeters, 96);
        assert(s.width() == 96 && s.height() == 192);
    }
    // Degenerate/zero input clamps to at least 1x1 so a blank QImage is
    // never constructed with a zero or negative dimension.
    {
        QSize s = computeCanvasPixelSize(0, -5, SizeUnit::Pixels, 300);
        assert(s.width() == 1 && s.height() == 1);
    }
    return 0;
}
```

- [ ] **Step 2: Add the test target to CMakeLists.txt and confirm it fails to build (header doesn't exist yet)**

Add, near the other `add_executable(..._test ...)` blocks (after the `adjustments_paint_test` block):
```cmake
add_executable(new_document_size_test tests/NewDocumentSizeTest.cpp src/edit/NewDocumentSize.cpp)
target_include_directories(new_document_size_test PRIVATE src)
target_link_libraries(new_document_size_test PRIVATE Qt6::Widgets)
add_test(NAME new_document_size_test COMMAND new_document_size_test)
```
Run:
```bash
cmake -S . -B build && cmake --build build --target new_document_size_test 2>&1 | tail -30
```
Expected: FAIL — `src/edit/NewDocumentSize.h` / `.cpp` don't exist yet.

- [ ] **Step 3: Write the header and implementation**

```cpp
// src/edit/NewDocumentSize.h
#pragma once

#include <QSize>

enum class SizeUnit { Pixels, Inches, Centimeters };

// Converts a File > New dialog's width/height/unit/dpi inputs into a pixel
// QSize, clamped to a minimum of 1x1 (a blank canvas can never be 0px).
QSize computeCanvasPixelSize(double width, double height, SizeUnit unit, double dpi);
```

```cpp
// src/edit/NewDocumentSize.cpp
#include "edit/NewDocumentSize.h"

#include <algorithm>
#include <cmath>

QSize computeCanvasPixelSize(double width, double height, SizeUnit unit, double dpi) {
    double wPx = width;
    double hPx = height;
    if (unit == SizeUnit::Inches) {
        wPx = width * dpi;
        hPx = height * dpi;
    } else if (unit == SizeUnit::Centimeters) {
        constexpr double kCmPerInch = 2.54;
        wPx = (width / kCmPerInch) * dpi;
        hPx = (height / kCmPerInch) * dpi;
    }
    int w = std::max(1, int(std::lround(wPx)));
    int h = std::max(1, int(std::lround(hPx)));
    return QSize(w, h);
}
```

- [ ] **Step 4: Build and run the test**

```bash
cmake --build build --target new_document_size_test 2>&1 | tail -30 && ./build/new_document_size_test && echo PASS
```
Expected: builds clean, exits 0, prints `PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/edit/NewDocumentSize.h src/edit/NewDocumentSize.cpp tests/NewDocumentSizeTest.cpp CMakeLists.txt
git commit -m "Add computeCanvasPixelSize helper with unit test"
```

---

### Task 4: New Document dialog + File menu action

**Files:**
- Create: `src/edit/NewDocumentDialog.h`
- Create: `src/edit/NewDocumentDialog.cpp`
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`
- Modify: `CMakeLists.txt` (main app sources)

**Interfaces:**
- Consumes: `computeCanvasPixelSize()` / `SizeUnit` from Task 3.
- Produces: `class NewDocumentDialog : public QDialog { public: explicit NewDocumentDialog(QWidget *parent=nullptr); QSize resultPixelSize() const; };` — Task 5 constructs one from `RetouchWindow::onNewDocument()` and reads `resultPixelSize()`.

- [ ] **Step 1: Write the dialog**

```cpp
// src/edit/NewDocumentDialog.h
#pragma once

#include <QDialog>
#include <QSize>

class QDoubleSpinBox;
class QComboBox;

// File > New: prompts for width/height/unit/DPI, exposes the resolved pixel
// size via resultPixelSize() after exec() returns QDialog::Accepted.
class NewDocumentDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDocumentDialog(QWidget *parent = nullptr);
    QSize resultPixelSize() const { return m_result; }

private slots:
    void onAccept();

private:
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QComboBox *m_unit = nullptr;
    QDoubleSpinBox *m_dpi = nullptr;
    QSize m_result;
};
```

```cpp
// src/edit/NewDocumentDialog.cpp
#include "edit/NewDocumentDialog.h"
#include "edit/NewDocumentSize.h"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>

NewDocumentDialog::NewDocumentDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("New Document");

    m_width = new QDoubleSpinBox;
    m_width->setRange(1, 100000);
    m_width->setValue(1920);

    m_height = new QDoubleSpinBox;
    m_height->setRange(1, 100000);
    m_height->setValue(1080);

    m_unit = new QComboBox;
    m_unit->addItem("Pixels", int(SizeUnit::Pixels));
    m_unit->addItem("Inches", int(SizeUnit::Inches));
    m_unit->addItem("Centimeters", int(SizeUnit::Centimeters));

    m_dpi = new QDoubleSpinBox;
    m_dpi->setRange(1, 2400);
    m_dpi->setValue(300);

    auto *form = new QFormLayout;
    form->addRow("Width:", m_width);
    form->addRow("Height:", m_height);
    form->addRow("Unit:", m_unit);
    form->addRow("Resolution (DPI):", m_dpi);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewDocumentDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void NewDocumentDialog::onAccept() {
    SizeUnit unit = SizeUnit(m_unit->currentData().toInt());
    m_result = computeCanvasPixelSize(m_width->value(), m_height->value(), unit, m_dpi->value());
    accept();
}
```

- [ ] **Step 2: Register the new sources in CMakeLists.txt**

Find the main app's `add_executable`/source list (the one that already includes `src/edit/RetouchWindow.cpp`, `src/edit/RetouchTab.cpp`, etc.) and add `src/edit/NewDocumentDialog.cpp` and `src/edit/NewDocumentSize.cpp` alongside them, matching the existing list's formatting.

- [ ] **Step 3: Add the File > New menu action**

In `RetouchWindow.h`, add a new private slot declaration near `onOpenPhotos`:
```cpp
    void onNewDocument();
```

In `RetouchWindow.cpp`, find:
```cpp
    auto *openSessionAction = new QAction("Open Session…", this);
    auto *openPhotosAction = new QAction("Open Photos…", this);
    connect(openSessionAction, &QAction::triggered, this, &RetouchWindow::onOpenSession);
    connect(openPhotosAction, &QAction::triggered, this, &RetouchWindow::onOpenPhotos);
```
Add immediately after:
```cpp
    auto *newDocAction = new QAction("New…", this);
    newDocAction->setShortcut(QKeySequence::New); // Ctrl+N
    connect(newDocAction, &QAction::triggered, this, &RetouchWindow::onNewDocument);
```
Find:
```cpp
    m_fileMenu = menuBar()->addMenu("File");
    m_fileMenu->addAction(openSessionAction);
    m_fileMenu->addAction(openPhotosAction);
```
Change to:
```cpp
    m_fileMenu = menuBar()->addMenu("File");
    m_fileMenu->addAction(newDocAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(openSessionAction);
    m_fileMenu->addAction(openPhotosAction);
```

Add a stub implementation (fleshed out in Task 5, once `RetouchTab` supports a pathless constructor) near `onOpenPhotos()`:
```cpp
void RetouchWindow::onNewDocument() {
    NewDocumentDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    createUntitledTab(dlg.resultPixelSize());
}
```
Add `#include "edit/NewDocumentDialog.h"` to `RetouchWindow.cpp`'s includes. Note `createUntitledTab` doesn't exist yet — this will fail to compile until Task 5 adds it; that's expected and resolved in the next task, so build verification for *this* task is limited to confirming the dialog and menu action compile in isolation.

- [ ] **Step 4: Build to confirm only the expected missing-symbol error**

```bash
cmake --build build 2>&1 | tail -30
```
Expected: a single linker/compiler error referencing `createUntitledTab` (undefined) — confirms everything else in this task compiles. Do not attempt to fix this error here; Task 5 resolves it.

- [ ] **Step 5: Commit**

```bash
git add src/edit/NewDocumentDialog.h src/edit/NewDocumentDialog.cpp src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp CMakeLists.txt
git commit -m "Add File > New dialog and menu action (tab creation wired in next commit)"
```

---

### Task 5: RetouchTab blank-canvas construction + untitled tab tracking

**Files:**
- Modify: `src/edit/RetouchTab.h`
- Modify: `src/edit/RetouchTab.cpp`
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: existing `RetouchTab(const QString &path, QWidget *parent)`, `m_base`, `m_path`, `rebuildGeom()`, `isReady()` (`!m_base.isNull()`).
- Produces: `explicit RetouchTab(const QSize &blankSize, QWidget *parent = nullptr);` (new overload — Task 5's own `createUntitledTab` is the only caller, but document it as the constructor future tasks would also use). `void RetouchWindow::createUntitledTab(const QSize &size);` — called by Task 4's `onNewDocument()`.

- [ ] **Step 1: Add the blank-canvas constructor to RetouchTab**

In `src/edit/RetouchTab.h`, add next to the existing constructor:
```cpp
    explicit RetouchTab(const QString &path, QWidget *parent = nullptr);
    explicit RetouchTab(const QSize &blankSize, QWidget *parent = nullptr); // File > New
```

In `src/edit/RetouchTab.cpp`, after the existing constructor (which ends around the `connect(m_canvas, ...)` block and `EditSidecar::load`/decode kickoff), add a new constructor that shares the widget-setup code. First, factor the shared canvas/connect setup out: locate where the existing constructor's body starts wiring `m_canvas` signals (`connect(m_canvas, &ImageCanvas::cropSelected, ...)` through the end of the constructor) — leave that code exactly where it is in the path-based constructor, and add a second constructor that does the blank-image-specific setup then delegates into the same wiring via a private helper. Concretely:

```cpp
RetouchTab::RetouchTab(const QSize &blankSize, QWidget *parent)
    : QWidget(parent), m_path(QString()) {
    m_base = QImage(blankSize, QImage::Format_ARGB32);
    m_base.fill(Qt::transparent);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new ImageCanvas;
    layout->addWidget(m_canvas);
    connect(m_canvas, &ImageCanvas::cropSelected, this, &RetouchTab::onCanvasCrop);
    connect(m_canvas, &ImageCanvas::commitCropRequested, this, &RetouchTab::applyCrop);
    connect(m_canvas, &ImageCanvas::colorPicked, this, &RetouchTab::onColorPicked);
    connect(m_canvas, &ImageCanvas::colorRangePickStarted, this,
            &RetouchTab::onColorRangePickStarted);
    connect(m_canvas, &ImageCanvas::colorRangeDragged, this,
            &RetouchTab::onColorRangeDragged);
    connect(m_canvas, &ImageCanvas::colorRangeReleased, this,
            &RetouchTab::onColorRangeReleased);
    // NOTE for implementer: copy every remaining connect(...) statement from
    // the path-based constructor here verbatim (heal/erase/mask/render-
    // worker wiring, m_renderThread setup, etc.) EXCEPT the
    // EditSidecar::load / kickoffImageLayerDecode / QtConcurrent RawLoader
    // decode block at the top, which this constructor skips entirely.
    // After all wiring, call rebuildGeom() and retone() synchronously (no
    // decode to wait for) so the canvas shows the blank image immediately,
    // then emit decoded(true).
    rebuildGeom();
    retone();
    emit decoded(true);
}
```

Since duplicating the entire wiring block verbatim is error-prone and this constructor is >80% identical to the existing one, refactor: extract everything from `auto *layout = new QVBoxLayout(this);` through the end of the current constructor (excluding the top `EditSidecar::load`/decode-kickoff lines) into a private method `void RetouchTab::setupCanvasAndWiring()`, call it from both constructors, and have the path-based constructor keep its `EditSidecar::load`/decode block before calling it, while the blank-canvas constructor sets `m_base` before calling it and skips straight to `rebuildGeom(); retone(); emit decoded(true);` after.

Add the private method declaration in `RetouchTab.h`:
```cpp
    void setupCanvasAndWiring();
```

- [ ] **Step 2: Add `createUntitledTab` and synthetic-key tracking to RetouchWindow**

In `RetouchWindow.h`, add near `m_openTabs`:
```cpp
    void createUntitledTab(const QSize &size);
    int m_untitledCounter = 0;
```

In `RetouchWindow.cpp`, add the definition (near `openPhoto()`):
```cpp
void RetouchWindow::createUntitledTab(const QSize &size) {
    QString key = QString("untitled:%1").arg(++m_untitledCounter);
    auto *tab = new RetouchTab(size);
    m_openTabs.insert(key, tab);
    int idx = m_tabs->addTab(tab, QString("Untitled-%1").arg(m_untitledCounter));
    m_tabs->setCurrentIndex(idx);
    setDockEnabled(true);
    syncDockFromTab();
    refreshHistoryPanel();
    refreshLevels();
    refreshMaskPanel();
    updateEditClipboardActions();
    m_statusLabel->setText(QString("Untitled-%1 ready").arg(m_untitledCounter));
}
```
This mirrors `openPhoto()`'s post-creation sync calls but skips `addToFilmstrip()` entirely (per the Global Constraints — untitled tabs never touch the filmstrip) and skips the async `decoded` signal connection since the blank-canvas constructor already emits `decoded(true)` synchronously before `createUntitledTab` finishes wiring — so wire post-creation sync directly (as above) rather than deferring to a `decoded` handler.

- [ ] **Step 3: Build**

```bash
cmake --build build 2>&1 | tail -40
```
Expected: clean build. This resolves Task 4's expected `createUntitledTab` linker error.

- [ ] **Step 4: Manual verification**

Run the app, trigger File > New with e.g. 800x600 px:
- Confirm a tab titled "Untitled-1" opens showing a blank/transparent canvas.
- Confirm it does NOT appear in the filmstrip.
- Confirm tone/color/paint/mask tools all work on it immediately (no "decoding" wait).
- Confirm no Background row appears in the Layers panel for this tab (per `refreshMaskPanel()`'s `!tab->path().isEmpty()` check from Task 2 — an empty `m_path` means `hasBackground` is `false`).

- [ ] **Step 5: Commit**

```bash
git add src/edit/RetouchTab.h src/edit/RetouchTab.cpp src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Support blank-canvas RetouchTab construction for File > New"
```

---

### Task 6: First-save "Save As" flow for untitled tabs

**Files:**
- Modify: `src/edit/RetouchTab.h`
- Modify: `src/edit/RetouchTab.cpp`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `RetouchTab::path()`, `RetouchTab::saveEdits()` (existing), `m_openTabs` (existing `QMap<QString, RetouchTab*>`).
- Produces: `void RetouchTab::assignPath(const QString &path);` — sets `m_path` and marks the tab dirty-for-resave; no other task consumes this beyond Task 6 itself, but documented for completeness since it mutates a previously-`const`-from-outside field.

- [ ] **Step 1: Add `assignPath` to RetouchTab**

In `RetouchTab.h`, add near `path()`:
```cpp
    void assignPath(const QString &path); // File > New's first save: adopt a real backing path
```

In `RetouchTab.cpp`, add:
```cpp
void RetouchTab::assignPath(const QString &path) {
    m_path = path;
}
```

- [ ] **Step 2: Extend `onSave()` to prompt for a location when the tab has no path**

Find:
```cpp
void RetouchWindow::onSave() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    tab->saveEdits();
    m_statusLabel->setText("Saved edits: " + QFileInfo(tab->path()).fileName());
}
```
Replace with:
```cpp
void RetouchWindow::onSave() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    if (tab->path().isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save As", QDir(QDir::homePath()).filePath("Pictures/Tether/untitled.png"),
            "PNG image (*.png)");
        if (path.isEmpty()) return; // user cancelled
        // Write the current base canvas to disk so the path has real image
        // content, then re-key the tab exactly like any file-backed tab.
        tab->renderFullRes().save(path, "PNG");
        QString oldKey;
        for (auto it = m_openTabs.begin(); it != m_openTabs.end(); ++it) {
            if (it.value() == tab) { oldKey = it.key(); break; }
        }
        if (!oldKey.isEmpty()) m_openTabs.remove(oldKey);
        tab->assignPath(path);
        m_openTabs.insert(path, tab);
        int idx = m_tabs->indexOf(tab);
        if (idx >= 0) m_tabs->setTabText(idx, QFileInfo(path).fileName());
        // Deliberately NOT calling addToFilmstrip(path) here — per spec,
        // auto-adding a saved-from-blank-canvas tab to the filmstrip is out
        // of scope. The user can add it via File > Open Photos later if
        // they want it in the strip.
    }
    tab->saveEdits();
    refreshMaskPanel(); // the tab now has a path -> Background row should appear
    m_statusLabel->setText("Saved edits: " + QFileInfo(tab->path()).fileName());
}
```

- [ ] **Step 3: Audit the two full `m_openTabs` iterations for empty-path tolerance**

Find `onSaveAll()`:
```cpp
void RetouchWindow::onSaveAll() {
    int n = 0;
    for (RetouchTab *tab : m_openTabs) {
        if (tab && tab->isReady() && tab->isDirty()) { tab->saveEdits(); ++n; }
    }
    m_statusLabel->setText(QString("Saved edits for %1 photo(s)").arg(n));
}
```
This iterates values only (not keys) and calls `saveEdits()` directly — for an untitled tab (empty `m_path`) this would call `EditSidecar::save("", ...)`, writing a sidecar next to no file. Guard it:
```cpp
void RetouchWindow::onSaveAll() {
    int n = 0;
    for (RetouchTab *tab : m_openTabs) {
        if (tab && tab->isReady() && tab->isDirty() && !tab->path().isEmpty()) {
            tab->saveEdits();
            ++n;
        }
    }
    m_statusLabel->setText(QString("Saved edits for %1 photo(s)").arg(n));
}
```
(Untitled tabs are simply skipped by Save All — the user must use Save on that tab directly to get the location prompt. This is a deliberate scope limit; documented in the spec's Out of Scope.)

The other `m_openTabs` iteration is `RetouchWindow::closeEvent()` — an autosave-on-close loop:
```cpp
void RetouchWindow::closeEvent(QCloseEvent *event) {
    for (RetouchTab *tab : m_openTabs) {
        if (tab && tab->isReady() && tab->isDirty()) tab->saveEdits();
    }
    ...
```
Apply the same guard, since an untitled tab's `m_path` is empty and `saveEdits()` would call `EditSidecar::save("", ...)`:
```cpp
void RetouchWindow::closeEvent(QCloseEvent *event) {
    for (RetouchTab *tab : m_openTabs) {
        if (tab && tab->isReady() && tab->isDirty() && !tab->path().isEmpty())
            tab->saveEdits();
    }
    ...
```
(Leave the rest of `closeEvent()` — `QSettings` geometry/state saving — untouched.) An unsaved untitled tab is silently discarded on window close under this guard; prompting the user to save-or-discard unsaved untitled tabs on close is out of scope for this plan.

- [ ] **Step 4: Build**

```bash
cmake --build build 2>&1 | tail -40
```
Expected: clean build.

- [ ] **Step 5: Manual verification**

- File > New (e.g. 400x300 px) → Ctrl+S → confirm a save dialog appears, choose a path → confirm the tab title changes from "Untitled-1" to the chosen filename and the Layers panel now shows the locked Background row. Confirm it does NOT appear in the filmstrip (out of scope, per spec).
- Confirm Save All does not throw/crash while an untitled tab is still open elsewhere and skips it (no dialog pops up from Save All).
- Re-open the saved file via File > Open Photos and confirm the edits persisted (sidecar loaded correctly) and it shows the locked Background row like any other opened photo.

- [ ] **Step 6: Commit**

```bash
git add src/edit/RetouchTab.h src/edit/RetouchTab.cpp src/edit/RetouchWindow.cpp
git commit -m "Prompt for save location on first save of an untitled tab"
```

---

### Task 7: Full regression pass

**Files:** none (verification only)

- [ ] **Step 1: Run the full test suite**

```bash
cmake --build build 2>&1 | tail -40 && ctest --test-dir build --output-on-failure
```
Expected: all tests pass, including the new `new_document_size_test` and the pre-existing `adjustments_paint_test`/`af_mapping_test`/etc.

- [ ] **Step 2: Manual end-to-end walkthrough**

- Open a photo from File > Open Photos: confirm Background row, duplicate flow, erase/heal gating (repeat Task 2 Step 4 checklist).
- Open a photo from the filmstrip (click an existing thumbnail): confirm identical Background-row behavior (this path also goes through `openPhoto()`, so no separate code path to break, but confirm it since it's explicitly named in the original request).
- File > New end-to-end (repeat Task 6 Step 5 checklist).
- Confirm existing features untouched: adding a mask layer, adding an image layer via drag/drop, reordering real mask layers, deleting a mask layer, undo/redo, Save/Save All/Export on ordinary file-backed tabs.

- [ ] **Step 3: Report results to the user**

Summarize what was tested and any deviations from the plan encountered during implementation, before considering this feature complete.
