# Per-Layer Adjustment Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the per-layer adjustment sections currently built inline inside `LayersPanel` (Tone, Colour, Tone Curve, Detail & Effects) into standalone `QDockWidget` panels, matching how `LevelsPanel` and `MaskPanel` already work — so all six (Tone, Color, Tone Curve, Levels, Detail & Effects, Masks) can be dragged out and floated independently, but tabify together with the Layers dock by default.

**Architecture:** Four new plain-`QWidget` panel classes (`TonePanel`, `ColorPanel`, `ToneCurvePanel`, `DetailEffectsPanel`) live in `src/ui/`, each following the existing `LevelsPanel` shape: a `setAdjustments(...)`/`clear()` pair to sync from the model, and a Qt signal emitted on user edits. `LayersPanel` shrinks to just the layer list + Name/Opacity/Blend. `RetouchWindow` gains one `QDockWidget` per new panel (plus a second, separate `LevelsPanel` instance for the per-layer Levels dock — distinct from the existing global-Adjustments `m_levelsPanel`), wires each panel's signal into a read-modify-write of the active layer's `MaskAdjust`, and extends `refreshMaskPanel()` to push the active layer's values into all six panels together.

**Tech Stack:** C++17, Qt6 Widgets, CMake/Ninja.

## Global Constraints

- Follow the existing codebase's dock pattern exactly (`LevelsPanel`/`MaskPanel` as built by `RetouchWindow::buildLevelsDock()`/`buildMaskDock()`) — do not invent a new docking mechanism.
- No changes to the global/main Adjustments dock (`RetouchWindow::buildDock()`) or its Orientation section.
- No new adjustment logic or `MaskAdjust`/`Mask` fields — this is a UI reorganization of existing controls and existing data.
- No change to `MaskPanel`'s behavior or the mask-shape editing UI.
- New docks get a unique `setObjectName()` so they're automatically covered by the existing `saveState()`/`restoreState()` panel-layout persistence (`docs/superpowers/specs/2026-07-19-persistent-panel-layout-design.md`) — no new persistence code needed.
- This project has no unit tests for view-only Qt widgets (`LayersPanel`/`MaskPanel`/`LevelsPanel` have none) — verification for each task is "it compiles" (`cmake --build build`), with a final manual smoke-test task covering runtime behavior.

---

## File Structure

New files:
- `src/ui/TonePanel.h` / `.cpp` — Brightness/Contrast/Highlights/Shadows
- `src/ui/ColorPanel.h` / `.cpp` — Saturation/Vibrance/Temperature/Tint
- `src/ui/ToneCurvePanel.h` / `.cpp` — thin wrapper around the existing `CurveEditor`
- `src/ui/DetailEffectsPanel.h` / `.cpp` — Clarity/Sharpen/Vignette

Modified files:
- `CMakeLists.txt` — register the four new `.cpp` files
- `src/ui/LayersPanel.h` / `.cpp` — remove the Tone/Colour/Curve/Levels/Detail sections and the `maskAdjustChanged`/`setLevelsPreviewImage` members
- `src/edit/RetouchWindow.h` — new dock/panel members, new `buildXDock()` declarations
- `src/edit/RetouchWindow.cpp` — new dock builders, `refreshMaskPanel()`, `applyDefaultDockLayout()`, `buildViewMenu()`, `applyModeChrome()`, `setDockEnabled()`, the K/Brush tool-toggle handlers, and the `maskPreviewUpdated` connection

---

### Task 1: TonePanel widget

**Files:**
- Create: `src/ui/TonePanel.h`
- Create: `src/ui/TonePanel.cpp`
- Modify: `CMakeLists.txt:33` (add source file)

**Interfaces:**
- Produces: `TonePanel::setAdjustments(int brightness, int contrast, int highlights, int shadows)`, `TonePanel::clear()`, signal `TonePanel::adjustChanged(int brightness, int contrast, int highlights, int shadows)`.

- [ ] **Step 1: Create `src/ui/TonePanel.h`**

```cpp
#pragma once

#include <QWidget>

class QSlider;

// Brightness/Contrast/Highlights/Shadows sliders for the selected layer's
// tone adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Tone" label, so no header is drawn here (matches LevelsPanel/MaskPanel).
class TonePanel : public QWidget {
    Q_OBJECT
public:
    explicit TonePanel(QWidget *parent = nullptr);

    void setAdjustments(int brightness, int contrast, int highlights, int shadows);
    void clear(); // no active layer

signals:
    void adjustChanged(int brightness, int contrast, int highlights, int shadows);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_highlights = nullptr;
    QSlider *m_shadows = nullptr;
};
```

- [ ] **Step 2: Create `src/ui/TonePanel.cpp`**

```cpp
#include "ui/TonePanel.h"

#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>

TonePanel::TonePanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *form = new QFormLayout;
    auto mk = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitChanged(); });
        return s;
    };
    m_brightness = mk("Brightness");
    m_contrast = mk("Contrast");
    m_highlights = mk("Highlights");
    m_shadows = mk("Shadows");
    root->addLayout(form);
    root->addStretch(1);

    clear();
}

void TonePanel::setAdjustments(int brightness, int contrast, int highlights, int shadows) {
    m_syncing = true;
    setEnabled(true);
    m_brightness->setValue(brightness);
    m_contrast->setValue(contrast);
    m_highlights->setValue(highlights);
    m_shadows->setValue(shadows);
    m_syncing = false;
}

void TonePanel::clear() {
    m_syncing = true;
    m_brightness->setValue(0);
    m_contrast->setValue(0);
    m_highlights->setValue(0);
    m_shadows->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void TonePanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_brightness->value(), m_contrast->value(),
                        m_highlights->value(), m_shadows->value());
}
```

- [ ] **Step 3: Register the new file in `CMakeLists.txt`**

In `CMakeLists.txt`, find the line `src/ui/LayersPanel.cpp` (line 33) and add the new source right after it:

```cmake
    src/ui/LayersPanel.cpp
    src/ui/TonePanel.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build`
Expected: build succeeds with no errors (Task 6 wires `TonePanel` into `RetouchWindow`; until then it's just compiled as an unused class, which is fine).

- [ ] **Step 5: Commit**

```bash
git add src/ui/TonePanel.h src/ui/TonePanel.cpp CMakeLists.txt
git commit -m "feat: add TonePanel widget for per-layer tone adjustments"
```

---

### Task 2: ColorPanel widget

**Files:**
- Create: `src/ui/ColorPanel.h`
- Create: `src/ui/ColorPanel.cpp`
- Modify: `CMakeLists.txt` (add source file)

**Interfaces:**
- Produces: `ColorPanel::setAdjustments(int saturation, int vibrance, int temperature, int tint)`, `ColorPanel::clear()`, signal `ColorPanel::adjustChanged(int saturation, int vibrance, int temperature, int tint)`.

- [ ] **Step 1: Create `src/ui/ColorPanel.h`**

```cpp
#pragma once

#include <QWidget>

class QSlider;

// Saturation/Vibrance/Temperature/Tint sliders for the selected layer's
// colour adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Color" label, so no header is drawn here (matches LevelsPanel/MaskPanel).
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget *parent = nullptr);

    void setAdjustments(int saturation, int vibrance, int temperature, int tint);
    void clear(); // no active layer

signals:
    void adjustChanged(int saturation, int vibrance, int temperature, int tint);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_saturation = nullptr;
    QSlider *m_vibrance = nullptr;
    QSlider *m_temperature = nullptr;
    QSlider *m_tint = nullptr;
};
```

- [ ] **Step 2: Create `src/ui/ColorPanel.cpp`**

```cpp
#include "ui/ColorPanel.h"

#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>

ColorPanel::ColorPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *form = new QFormLayout;
    auto mk = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitChanged(); });
        return s;
    };
    m_saturation = mk("Saturation");
    m_vibrance = mk("Vibrance");
    m_temperature = mk("Temperature");
    m_tint = mk("Tint (green/magenta)");
    root->addLayout(form);
    root->addStretch(1);

    clear();
}

void ColorPanel::setAdjustments(int saturation, int vibrance, int temperature, int tint) {
    m_syncing = true;
    setEnabled(true);
    m_saturation->setValue(saturation);
    m_vibrance->setValue(vibrance);
    m_temperature->setValue(temperature);
    m_tint->setValue(tint);
    m_syncing = false;
}

void ColorPanel::clear() {
    m_syncing = true;
    m_saturation->setValue(0);
    m_vibrance->setValue(0);
    m_temperature->setValue(0);
    m_tint->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void ColorPanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_saturation->value(), m_vibrance->value(),
                        m_temperature->value(), m_tint->value());
}
```

- [ ] **Step 3: Register the new file in `CMakeLists.txt`**

Add right after `src/ui/TonePanel.cpp`:

```cmake
    src/ui/TonePanel.cpp
    src/ui/ColorPanel.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build`
Expected: build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ColorPanel.h src/ui/ColorPanel.cpp CMakeLists.txt
git commit -m "feat: add ColorPanel widget for per-layer colour adjustments"
```

---

### Task 3: ToneCurvePanel widget

**Files:**
- Create: `src/ui/ToneCurvePanel.h`
- Create: `src/ui/ToneCurvePanel.cpp`
- Modify: `CMakeLists.txt` (add source file)

**Interfaces:**
- Consumes: `CurveEditor` (`src/edit/CurveEditor.h`) — `setCurve(const QVector<QPointF>&)` (no signal), `resetCurve()`, signal `curveChanged(const QVector<QPointF>&)`.
- Produces: `ToneCurvePanel::setCurve(const QVector<QPointF>&)`, `ToneCurvePanel::clear()`, signal `ToneCurvePanel::curveChanged(const QVector<QPointF>&)`.

- [ ] **Step 1: Create `src/ui/ToneCurvePanel.h`**

```cpp
#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

class CurveEditor;

// Thin wrapper hosting the tone-curve editor for the selected layer, as its
// own dockable panel. The dock title provides the "Tone Curve" label.
class ToneCurvePanel : public QWidget {
    Q_OBJECT
public:
    explicit ToneCurvePanel(QWidget *parent = nullptr);

    void setCurve(const QVector<QPointF> &points); // no signal
    void clear(); // reset to identity + disable

signals:
    void curveChanged(const QVector<QPointF> &points);

private:
    CurveEditor *m_curve = nullptr;
};
```

- [ ] **Step 2: Create `src/ui/ToneCurvePanel.cpp`**

```cpp
#include "ui/ToneCurvePanel.h"

#include "edit/CurveEditor.h"

#include <QVBoxLayout>

ToneCurvePanel::ToneCurvePanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    m_curve = new CurveEditor;
    root->addWidget(m_curve);
    connect(m_curve, &CurveEditor::curveChanged, this, &ToneCurvePanel::curveChanged);
    setEnabled(false);
}

void ToneCurvePanel::setCurve(const QVector<QPointF> &points) {
    setEnabled(true);
    m_curve->setCurve(points);
}

void ToneCurvePanel::clear() {
    m_curve->resetCurve();
    setEnabled(false);
}
```

- [ ] **Step 3: Register the new file in `CMakeLists.txt`**

Add right after `src/ui/ColorPanel.cpp`:

```cmake
    src/ui/ColorPanel.cpp
    src/ui/ToneCurvePanel.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build`
Expected: build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ToneCurvePanel.h src/ui/ToneCurvePanel.cpp CMakeLists.txt
git commit -m "feat: add ToneCurvePanel widget wrapping CurveEditor"
```

---

### Task 4: DetailEffectsPanel widget

**Files:**
- Create: `src/ui/DetailEffectsPanel.h`
- Create: `src/ui/DetailEffectsPanel.cpp`
- Modify: `CMakeLists.txt` (add source file)

**Interfaces:**
- Produces: `DetailEffectsPanel::setAdjustments(int clarity, int sharpen, int vignette)`, `DetailEffectsPanel::clear()`, signal `DetailEffectsPanel::adjustChanged(int clarity, int sharpen, int vignette)`.

- [ ] **Step 1: Create `src/ui/DetailEffectsPanel.h`**

```cpp
#pragma once

#include <QWidget>

class QSlider;

// Clarity/Sharpen/Vignette sliders for the selected layer's detail/effects
// adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Detail & Effects" label, so no header is drawn here (matches
// LevelsPanel/MaskPanel).
class DetailEffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailEffectsPanel(QWidget *parent = nullptr);

    void setAdjustments(int clarity, int sharpen, int vignette);
    void clear(); // no active layer

signals:
    void adjustChanged(int clarity, int sharpen, int vignette);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_clarity = nullptr;
    QSlider *m_sharpen = nullptr;
    QSlider *m_vignette = nullptr;
};
```

- [ ] **Step 2: Create `src/ui/DetailEffectsPanel.cpp`**

```cpp
#include "ui/DetailEffectsPanel.h"

#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>

DetailEffectsPanel::DetailEffectsPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *form = new QFormLayout;
    m_clarity = new QSlider(Qt::Horizontal);
    m_clarity->setRange(-100, 100);
    form->addRow("Clarity:", m_clarity);
    m_sharpen = new QSlider(Qt::Horizontal);
    m_sharpen->setRange(0, 100);
    form->addRow("Sharpen:", m_sharpen);
    m_vignette = new QSlider(Qt::Horizontal);
    m_vignette->setRange(-100, 100);
    form->addRow("Vignette:", m_vignette);
    root->addLayout(form);
    root->addStretch(1);

    connect(m_clarity, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_sharpen, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_vignette, &QSlider::valueChanged, this, [this] { emitChanged(); });

    clear();
}

void DetailEffectsPanel::setAdjustments(int clarity, int sharpen, int vignette) {
    m_syncing = true;
    setEnabled(true);
    m_clarity->setValue(clarity);
    m_sharpen->setValue(sharpen);
    m_vignette->setValue(vignette);
    m_syncing = false;
}

void DetailEffectsPanel::clear() {
    m_syncing = true;
    m_clarity->setValue(0);
    m_sharpen->setValue(0);
    m_vignette->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void DetailEffectsPanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_clarity->value(), m_sharpen->value(), m_vignette->value());
}
```

- [ ] **Step 3: Register the new file in `CMakeLists.txt`**

Add right after `src/ui/ToneCurvePanel.cpp`:

```cmake
    src/ui/ToneCurvePanel.cpp
    src/ui/DetailEffectsPanel.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build`
Expected: build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/DetailEffectsPanel.h src/ui/DetailEffectsPanel.cpp CMakeLists.txt
git commit -m "feat: add DetailEffectsPanel widget for per-layer detail/effects"
```

---

### Task 5: Shrink LayersPanel to just the layer list + Name/Opacity/Blend

**Files:**
- Modify: `src/ui/LayersPanel.h` (full rewrite)
- Modify: `src/ui/LayersPanel.cpp` (full rewrite)

**Interfaces:**
- Consumes: nothing new.
- Produces: `LayersPanel` keeps `setMasks(const QVector<Mask>&, int)`, `clear()`, and signals `addMaskRequested`, `addImageLayerRequested`, `selectMaskRequested`, `deleteMaskRequested`, `duplicateMaskRequested`, `maskOpacityChanged`, `maskBlendChanged`, `maskVisibleChanged`, `maskNameChanged`, `maskReorderRequested`. **Removed**: signal `maskAdjustChanged` and method `setLevelsPreviewImage` (both move to the new per-layer panels/docks in Task 6).

This task will not compile standalone (Task 6 removes the now-dangling call sites in `RetouchWindow.cpp`) — build verification happens at the end of Task 6. Do not run a build after this task in isolation.

- [ ] **Step 1: Rewrite `src/ui/LayersPanel.h`**

```cpp
#pragma once

#include <QVector>
#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, and blend mode. Tone/Colour/Tone Curve/Levels/Detail & Effects
// adjustments and mask shape editing each live in their own dock (TonePanel,
// ColorPanel, ToneCurvePanel, the per-layer LevelsPanel, DetailEffectsPanel,
// MaskPanel) — see RetouchWindow::refreshMaskPanel(), which keeps all of them
// in sync with the selected layer. Purely a view — it emits intent signals
// and is refreshed via setMasks(); RetouchWindow routes to the tab.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setMasks(const QVector<Mask> &masks, int activeIndex);
    void clear();

signals:
    void addMaskRequested();
    void addImageLayerRequested(const QString &path); // "Add Image Layer…" chosen a file
    void selectMaskRequested(int index);
    void deleteMaskRequested();
    void duplicateMaskRequested();
    void maskOpacityChanged(double opacity); // 0..1
    void maskBlendChanged(BlendMode mode);
    void maskVisibleChanged(int index, bool visible);
    void maskNameChanged(const QString &name);
    void maskReorderRequested(int from, int to);

private:
    void loadActive();
    void rebuildList();

    QVector<Mask> m_masks;
    int m_active = -1;
    bool m_syncing = false;

    QListWidget *m_maskList = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_delete = nullptr;

    QLineEdit *m_name = nullptr;
    QSlider *m_opacity = nullptr;
    QComboBox *m_blend = nullptr;
};
```

- [ ] **Step 2: Rewrite `src/ui/LayersPanel.cpp`**

```cpp
#include "ui/LayersPanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString maskTypeLabel(MaskType t) {
    switch (t) {
    case MaskType::Radial: return "Radial";
    case MaskType::Linear: return "Graduated";
    case MaskType::Brush:  return "Brush";
    case MaskType::Paint:  return "Paint";
    case MaskType::None:   return "Layer";
    }
    return "Layer";
}
} // namespace

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Layer list: checkable (visibility) rows, drag to reorder, Duplicate/Delete.
    auto *selRow = new QHBoxLayout;
    m_maskList = new QListWidget;
    m_maskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_maskList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_maskList->setMinimumHeight(140);
    selRow->addWidget(m_maskList, 1);
    auto *listButtons = new QVBoxLayout;
    m_add = new QPushButton("Add");
    m_duplicate = new QPushButton("Duplicate");
    m_delete = new QPushButton("Delete");
    listButtons->addWidget(m_add);
    listButtons->addWidget(m_duplicate);
    listButtons->addWidget(m_delete);
    listButtons->addStretch(1);
    selRow->addLayout(listButtons);
    root->addLayout(selRow, 1);

    // Name / opacity / blend mode for the selected layer.
    auto *props = new QFormLayout;
    m_name = new QLineEdit;
    props->addRow("Name:", m_name);
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    props->addRow("Opacity:", m_opacity);
    m_blend = new QComboBox;
    m_blend->addItem("Normal", int(BlendMode::Normal));
    m_blend->addItem("Multiply", int(BlendMode::Multiply));
    m_blend->addItem("Screen", int(BlendMode::Screen));
    m_blend->addItem("Overlay", int(BlendMode::Overlay));
    m_blend->addItem("Soft Light", int(BlendMode::SoftLight));
    props->addRow("Blend:", m_blend);
    root->addLayout(props);

    auto *addMenu = new QMenu(m_add);
    QAction *addLayerAction = addMenu->addAction("Add Layer");
    connect(addLayerAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(); });
    QAction *addImageAction = addMenu->addAction("Add Image Layer…");
    connect(addImageAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(
            this, "Add Image Layer", QString(),
            "Images (*.jpg *.jpeg *.png *.tif *.tiff *.nef *.NEF);;All Files (*)");
        if (!path.isEmpty()) emit addImageLayerRequested(path);
    });
    m_add->setMenu(addMenu);

    connect(m_duplicate, &QPushButton::clicked, this,
            [this] { emit duplicateMaskRequested(); });
    connect(m_delete, &QPushButton::clicked, this,
            [this] { emit deleteMaskRequested(); });
    connect(m_maskList, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!m_syncing) emit selectMaskRequested(i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                emit maskVisibleChanged(i, item->checkState() == Qt::Checked);
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &,
                   int destRow) {
                if (m_syncing) return;
                int to = destRow > start ? destRow - 1 : destRow;
                emit maskReorderRequested(start, to);
            });
    connect(m_name, &QLineEdit::editingFinished, this,
            [this] { if (!m_syncing) emit maskNameChanged(m_name->text()); });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing) emit maskOpacityChanged(v / 100.0);
    });
    connect(m_blend, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_syncing) return;
                emit maskBlendChanged(BlendMode(m_blend->currentData().toInt()));
            });

    clear();
}

void LayersPanel::clear() {
    m_masks.clear();
    m_active = -1;
    m_syncing = true;
    m_maskList->clear();
    m_syncing = false;
    setEnabled(false);
}

void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex) {
    m_masks = masks;
    m_active = activeIndex;
    setEnabled(true);
    rebuildList();
    loadActive();
}

void LayersPanel::rebuildList() {
    m_syncing = true;
    m_maskList->clear();
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
    if (m_active >= 0 && m_active < m_masks.size())
        m_maskList->setCurrentRow(m_active);
    m_syncing = false;
}

void LayersPanel::loadActive() {
    const bool has = m_active >= 0 && m_active < m_masks.size();
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_duplicate, m_delete})
        w->setEnabled(has);
    if (has) {
        const Mask &m = m_masks[m_active];
        m_name->setText(m.name);
        m_opacity->setValue(int(std::lround(m.opacity * 100)));
        int blendIdx = m_blend->findData(int(m.blend));
        m_blend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);
    }
    m_syncing = false;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/ui/LayersPanel.h src/ui/LayersPanel.cpp
git commit -m "refactor: shrink LayersPanel to layer list + name/opacity/blend"
```

---

### Task 6: Wire the new panels into RetouchWindow

**Files:**
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `TonePanel`, `ColorPanel`, `ToneCurvePanel`, `DetailEffectsPanel` (Tasks 1-4), shrunk `LayersPanel` (Task 5), existing `LevelsPanel`, `MaskPanel`, `MaskAdjust`, `RetouchTab::activeMaskIndex()`, `RetouchTab::masks()`, `RetouchTab::setActiveMaskAdjust(const MaskAdjust&)`.
- Produces: nothing consumed by later tasks — this is the final integration point.

- [ ] **Step 1: Add forward declarations to `src/edit/RetouchWindow.h`**

Find (around line 28-30):

```cpp
class LevelsPanel;
class MaskPanel;
class LayersPanel;
```

Replace with:

```cpp
class LevelsPanel;
class MaskPanel;
class LayersPanel;
class TonePanel;
class ColorPanel;
class ToneCurvePanel;
class DetailEffectsPanel;
```

- [ ] **Step 2: Add new dock/panel members and method declarations to `src/edit/RetouchWindow.h`**

Find (around line 153-159):

```cpp
    QDockWidget *m_layersDock = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    QDockWidget *m_maskDock = nullptr;
    MaskPanel *m_maskPanel = nullptr;
    void buildLayersDock();
    void buildMaskDock();
    void refreshMaskPanel(); // refreshes both Layers and Masks panels
```

Replace with:

```cpp
    QDockWidget *m_layersDock = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    QDockWidget *m_tonePanelDock = nullptr;
    TonePanel *m_tonePanel = nullptr;
    QDockWidget *m_colorPanelDock = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    QDockWidget *m_toneCurveDock = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    QDockWidget *m_layerLevelsDock = nullptr;
    LevelsPanel *m_layerLevelsPanel = nullptr; // per-layer Levels, distinct from the global m_levelsPanel
    QDockWidget *m_detailEffectsDock = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    QDockWidget *m_maskDock = nullptr;
    MaskPanel *m_maskPanel = nullptr;
    void buildLayersDock();
    void buildTonePanelDock();
    void buildColorPanelDock();
    void buildToneCurveDock();
    void buildLayerLevelsDock();
    void buildDetailEffectsDock();
    void buildMaskDock();
    void refreshMaskPanel(); // refreshes Layers, Tone, Color, Tone Curve, Levels, Detail & Effects, and Masks panels
```

- [ ] **Step 3: Build the new docks in the constructor**

In `src/edit/RetouchWindow.cpp`, find (around line 344-349):

```cpp
    buildToolPanel();
    buildToolOptionsBar();
    buildDock();
    buildHistoryDock();
    buildLevelsDock();
    buildLayersDock();
    buildMaskDock();
    buildViewMenu();
```

Replace with:

```cpp
    buildToolPanel();
    buildToolOptionsBar();
    buildDock();
    buildHistoryDock();
    buildLevelsDock();
    buildLayersDock();
    buildTonePanelDock();
    buildColorPanelDock();
    buildToneCurveDock();
    buildLayerLevelsDock();
    buildDetailEffectsDock();
    buildMaskDock();
    buildViewMenu();
```

- [ ] **Step 4: Add View menu entries**

Find (around line 454-461):

```cpp
    if (m_layersDock) viewMenu->addAction(m_layersDock->toggleViewAction());
    if (m_maskDock) viewMenu->addAction(m_maskDock->toggleViewAction());
```

Replace with:

```cpp
    if (m_layersDock) viewMenu->addAction(m_layersDock->toggleViewAction());
    if (m_tonePanelDock) viewMenu->addAction(m_tonePanelDock->toggleViewAction());
    if (m_colorPanelDock) viewMenu->addAction(m_colorPanelDock->toggleViewAction());
    if (m_toneCurveDock) viewMenu->addAction(m_toneCurveDock->toggleViewAction());
    if (m_layerLevelsDock) viewMenu->addAction(m_layerLevelsDock->toggleViewAction());
    if (m_detailEffectsDock) viewMenu->addAction(m_detailEffectsDock->toggleViewAction());
    if (m_maskDock) viewMenu->addAction(m_maskDock->toggleViewAction());
```

- [ ] **Step 5: Update `applyDefaultDockLayout()` to tabify and show all six docks with Layers**

Find the full method (around line 486-516):

```cpp
void RetouchWindow::applyDefaultDockLayout() {
    for (QDockWidget *d : {m_levelsDock, m_adjustmentsDock, m_historyDock,
                           m_layersDock, m_maskDock, m_controlsDock}) {
        if (d) {
            d->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, d);
        }
    }
    if (m_adjustmentsDock && m_historyDock)
        tabifyDockWidget(m_adjustmentsDock, m_historyDock);
    if (m_adjustmentsDock && m_layersDock)
        tabifyDockWidget(m_adjustmentsDock, m_layersDock);
    if (m_layersDock && m_maskDock)
        tabifyDockWidget(m_layersDock, m_maskDock);
    if (m_levelsDock && m_adjustmentsDock)
        splitDockWidget(m_levelsDock, m_adjustmentsDock, Qt::Vertical);
    if (m_toolsBar)
        addToolBar(Qt::LeftToolBarArea, m_toolsBar);

    // Default visibility for the persistent editing docks.
    if (m_adjustmentsDock) m_adjustmentsDock->show();
    if (m_historyDock)     m_historyDock->show();
    if (m_levelsDock)      m_levelsDock->show();
    if (m_layersDock)      m_layersDock->show();
    if (m_maskDock)        m_maskDock->show();

    // Let mode/tool chrome have the final say on editing-dock/Controls/Tools
    // visibility.
    const bool tether = m_tetherModeAction && m_tetherModeAction->isChecked();
    applyModeChrome(tether ? Mode::Tether : Mode::Retouch);
}
```

Replace with:

```cpp
void RetouchWindow::applyDefaultDockLayout() {
    for (QDockWidget *d : {m_levelsDock, m_adjustmentsDock, m_historyDock,
                           m_layersDock, m_tonePanelDock, m_colorPanelDock,
                           m_toneCurveDock, m_layerLevelsDock,
                           m_detailEffectsDock, m_maskDock, m_controlsDock}) {
        if (d) {
            d->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, d);
        }
    }
    if (m_adjustmentsDock && m_historyDock)
        tabifyDockWidget(m_adjustmentsDock, m_historyDock);
    if (m_adjustmentsDock && m_layersDock)
        tabifyDockWidget(m_adjustmentsDock, m_layersDock);
    for (QDockWidget *d : {m_tonePanelDock, m_colorPanelDock, m_toneCurveDock,
                           m_layerLevelsDock, m_detailEffectsDock, m_maskDock}) {
        if (m_layersDock && d) tabifyDockWidget(m_layersDock, d);
    }
    if (m_levelsDock && m_adjustmentsDock)
        splitDockWidget(m_levelsDock, m_adjustmentsDock, Qt::Vertical);
    if (m_toolsBar)
        addToolBar(Qt::LeftToolBarArea, m_toolsBar);

    // Default visibility for the persistent editing docks.
    if (m_adjustmentsDock) m_adjustmentsDock->show();
    if (m_historyDock)     m_historyDock->show();
    if (m_levelsDock)      m_levelsDock->show();
    if (m_layersDock)      m_layersDock->show();
    if (m_tonePanelDock)     m_tonePanelDock->show();
    if (m_colorPanelDock)    m_colorPanelDock->show();
    if (m_toneCurveDock)     m_toneCurveDock->show();
    if (m_layerLevelsDock)   m_layerLevelsDock->show();
    if (m_detailEffectsDock) m_detailEffectsDock->show();
    if (m_maskDock)        m_maskDock->show();

    // Let mode/tool chrome have the final say on editing-dock/Controls/Tools
    // visibility.
    const bool tether = m_tetherModeAction && m_tetherModeAction->isChecked();
    applyModeChrome(tether ? Mode::Tether : Mode::Retouch);
}
```

- [ ] **Step 6: Add the five new `buildXDock()` methods and remove `LayersPanel::maskAdjustChanged` wiring**

In `buildLayersDock()` (around line 1034-1101), find and remove this block (the signal no longer exists on `LayersPanel` after Task 5):

```cpp
    connect(m_layersPanel, &LayersPanel::maskAdjustChanged, this,
            [this](const MaskAdjust &a) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskAdjust(a);
            });
```

Immediately after the closing brace of `buildLayersDock()` (right before `void RetouchWindow::buildMaskDock() {`), insert the five new methods:

```cpp
void RetouchWindow::buildTonePanelDock() {
    auto *dock = new QDockWidget("Tone", this);
    m_tonePanelDock = dock;
    dock->setObjectName("tonePanelDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_tonePanel = new TonePanel;
    dock->setWidget(m_tonePanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_layersDock) tabifyDockWidget(m_layersDock, dock);
    dock->hide(); // default visibility set by applyDefaultDockLayout/mode chrome

    connect(m_tonePanel, &TonePanel::adjustChanged, this,
            [this](int brightness, int contrast, int highlights, int shadows) {
                RetouchTab *tab = currentTab();
                if (!tab) return;
                int idx = tab->activeMaskIndex();
                if (idx < 0 || idx >= tab->masks().size()) return;
                MaskAdjust a = tab->masks()[idx].adj;
                a.brightness = brightness;
                a.contrast = contrast;
                a.highlights = highlights;
                a.shadows = shadows;
                tab->setActiveMaskAdjust(a);
            });
}

void RetouchWindow::buildColorPanelDock() {
    auto *dock = new QDockWidget("Color", this);
    m_colorPanelDock = dock;
    dock->setObjectName("colorPanelDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_colorPanel = new ColorPanel;
    dock->setWidget(m_colorPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_layersDock) tabifyDockWidget(m_layersDock, dock);
    dock->hide();

    connect(m_colorPanel, &ColorPanel::adjustChanged, this,
            [this](int saturation, int vibrance, int temperature, int tint) {
                RetouchTab *tab = currentTab();
                if (!tab) return;
                int idx = tab->activeMaskIndex();
                if (idx < 0 || idx >= tab->masks().size()) return;
                MaskAdjust a = tab->masks()[idx].adj;
                a.saturation = saturation;
                a.vibrance = vibrance;
                a.temperature = temperature;
                a.tint = tint;
                tab->setActiveMaskAdjust(a);
            });
}

void RetouchWindow::buildToneCurveDock() {
    auto *dock = new QDockWidget("Tone Curve", this);
    m_toneCurveDock = dock;
    dock->setObjectName("toneCurveDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_toneCurvePanel = new ToneCurvePanel;
    dock->setWidget(m_toneCurvePanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_layersDock) tabifyDockWidget(m_layersDock, dock);
    dock->hide();

    connect(m_toneCurvePanel, &ToneCurvePanel::curveChanged, this,
            [this](const QVector<QPointF> &curve) {
                RetouchTab *tab = currentTab();
                if (!tab) return;
                int idx = tab->activeMaskIndex();
                if (idx < 0 || idx >= tab->masks().size()) return;
                MaskAdjust a = tab->masks()[idx].adj;
                a.curve = curve;
                tab->setActiveMaskAdjust(a);
            });
}

void RetouchWindow::buildLayerLevelsDock() {
    auto *dock = new QDockWidget("Levels", this);
    m_layerLevelsDock = dock;
    dock->setObjectName("layerLevelsDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_layerLevelsPanel = new LevelsPanel;
    dock->setWidget(m_layerLevelsPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_layersDock) tabifyDockWidget(m_layersDock, dock);
    dock->hide();

    connect(m_layerLevelsPanel, &LevelsPanel::levelsChanged, this,
            [this](const Levels &lv) {
                RetouchTab *tab = currentTab();
                if (!tab) return;
                int idx = tab->activeMaskIndex();
                if (idx < 0 || idx >= tab->masks().size()) return;
                MaskAdjust a = tab->masks()[idx].adj;
                a.levels = lv;
                tab->setActiveMaskAdjust(a);
            });
}

void RetouchWindow::buildDetailEffectsDock() {
    auto *dock = new QDockWidget("Detail & Effects", this);
    m_detailEffectsDock = dock;
    dock->setObjectName("detailEffectsDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_detailEffectsPanel = new DetailEffectsPanel;
    dock->setWidget(m_detailEffectsPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_layersDock) tabifyDockWidget(m_layersDock, dock);
    dock->hide();

    connect(m_detailEffectsPanel, &DetailEffectsPanel::adjustChanged, this,
            [this](int clarity, int sharpen, int vignette) {
                RetouchTab *tab = currentTab();
                if (!tab) return;
                int idx = tab->activeMaskIndex();
                if (idx < 0 || idx >= tab->masks().size()) return;
                MaskAdjust a = tab->masks()[idx].adj;
                a.clarity = clarity;
                a.sharpen = sharpen;
                a.vignette = vignette;
                tab->setActiveMaskAdjust(a);
            });
}
```

- [ ] **Step 7: Update `refreshMaskPanel()` to push into all six panels**

Find the full method (around line 1125-1138, note the line numbers shift after Step 6's insertions but the content is unique enough to locate):

```cpp
void RetouchWindow::refreshMaskPanel() {
    RetouchTab *tab = currentTab();
    const bool ready = tab && tab->isReady();
    if (m_layersPanel) {
        if (ready) m_layersPanel->setMasks(tab->masks(), tab->activeMaskIndex());
        else m_layersPanel->clear();
    }
    if (m_maskPanel) {
        const int idx = ready ? tab->activeMaskIndex() : -1;
        const bool hasSelection = ready && idx >= 0 && idx < tab->masks().size();
        if (hasSelection) m_maskPanel->setMask(tab->masks()[idx], true);
        else m_maskPanel->clear();
    }
}
```

Replace with:

```cpp
void RetouchWindow::refreshMaskPanel() {
    RetouchTab *tab = currentTab();
    const bool ready = tab && tab->isReady();
    if (m_layersPanel) {
        if (ready) m_layersPanel->setMasks(tab->masks(), tab->activeMaskIndex());
        else m_layersPanel->clear();
    }
    const int idx = ready ? tab->activeMaskIndex() : -1;
    const bool hasSelection = ready && idx >= 0 && idx < tab->masks().size();
    if (hasSelection) {
        const MaskAdjust &a = tab->masks()[idx].adj;
        if (m_tonePanel) m_tonePanel->setAdjustments(a.brightness, a.contrast, a.highlights, a.shadows);
        if (m_colorPanel) m_colorPanel->setAdjustments(a.saturation, a.vibrance, a.temperature, a.tint);
        if (m_toneCurvePanel) m_toneCurvePanel->setCurve(a.curve);
        if (m_layerLevelsPanel) {
            m_layerLevelsPanel->setEnabled(true);
            m_layerLevelsPanel->setLevels(a.levels);
        }
        if (m_detailEffectsPanel) m_detailEffectsPanel->setAdjustments(a.clarity, a.sharpen, a.vignette);
    } else {
        if (m_tonePanel) m_tonePanel->clear();
        if (m_colorPanel) m_colorPanel->clear();
        if (m_toneCurvePanel) m_toneCurvePanel->clear();
        if (m_layerLevelsPanel) {
            m_layerLevelsPanel->clear();
            m_layerLevelsPanel->setEnabled(false);
        }
        if (m_detailEffectsPanel) m_detailEffectsPanel->clear();
    }
    if (m_maskPanel) {
        if (hasSelection) m_maskPanel->setMask(tab->masks()[idx], true);
        else m_maskPanel->clear();
    }
}
```

- [ ] **Step 8: Redirect the mask-preview-image feed to the new per-layer Levels panel**

Find (around line 1364-1367):

```cpp
    connect(tab, &RetouchTab::maskPreviewUpdated, this, [this, tab] {
        if (tab == currentTab() && m_layersPanel)
            m_layersPanel->setLevelsPreviewImage(tab->maskPreviewImage());
    });
```

Replace with:

```cpp
    connect(tab, &RetouchTab::maskPreviewUpdated, this, [this, tab] {
        if (tab == currentTab() && m_layerLevelsPanel)
            m_layerLevelsPanel->setImage(tab->maskPreviewImage());
    });
```

- [ ] **Step 9: Extend `applyModeChrome()` to hide/show the new docks with tether mode**

Find (around line 1412-1418):

```cpp
    if (m_toolsBar)        m_toolsBar->setVisible(!tether);
    if (m_adjustmentsDock) m_adjustmentsDock->setVisible(!tether);
    if (m_historyDock)     m_historyDock->setVisible(!tether);
    if (m_layersDock)      m_layersDock->setVisible(!tether);
    if (m_maskDock)        m_maskDock->setVisible(!tether);
```

Replace with:

```cpp
    if (m_toolsBar)        m_toolsBar->setVisible(!tether);
    if (m_adjustmentsDock) m_adjustmentsDock->setVisible(!tether);
    if (m_historyDock)     m_historyDock->setVisible(!tether);
    if (m_layersDock)      m_layersDock->setVisible(!tether);
    if (m_tonePanelDock)     m_tonePanelDock->setVisible(!tether);
    if (m_colorPanelDock)    m_colorPanelDock->setVisible(!tether);
    if (m_toneCurveDock)     m_toneCurveDock->setVisible(!tether);
    if (m_layerLevelsDock)   m_layerLevelsDock->setVisible(!tether);
    if (m_detailEffectsDock) m_detailEffectsDock->setVisible(!tether);
    if (m_maskDock)        m_maskDock->setVisible(!tether);
```

- [ ] **Step 10: Extend `setDockEnabled()` to clear the new panels when no photo is ready**

Find (around line 1592-1594):

```cpp
    if (!enabled && m_levelsPanel) m_levelsPanel->clear();
    if (!enabled && m_layersPanel) m_layersPanel->clear();
    if (!enabled && m_maskPanel) m_maskPanel->clear();
```

Replace with:

```cpp
    if (!enabled && m_levelsPanel) m_levelsPanel->clear();
    if (!enabled && m_layersPanel) m_layersPanel->clear();
    if (!enabled && m_tonePanel) m_tonePanel->clear();
    if (!enabled && m_colorPanel) m_colorPanel->clear();
    if (!enabled && m_toneCurvePanel) m_toneCurvePanel->clear();
    if (!enabled && m_layerLevelsPanel) {
        m_layerLevelsPanel->clear();
        m_layerLevelsPanel->setEnabled(false);
    }
    if (!enabled && m_detailEffectsPanel) m_detailEffectsPanel->clear();
    if (!enabled && m_maskPanel) m_maskPanel->clear();
```

- [ ] **Step 11: Raise the new docks alongside Layers/Masks when the Mask (K) tool activates**

Find, inside the `m_maskToggle` toggled handler (around line 651-652):

```cpp
            if (m_layersDock) { m_layersDock->show(); m_layersDock->raise(); }
            if (m_maskDock) { m_maskDock->show(); m_maskDock->raise(); }
```

Replace with:

```cpp
            for (QDockWidget *d : {m_layersDock, m_tonePanelDock, m_colorPanelDock,
                                   m_toneCurveDock, m_layerLevelsDock,
                                   m_detailEffectsDock, m_maskDock}) {
                if (d) { d->show(); d->raise(); }
            }
```

- [ ] **Step 12: Raise the new docks alongside Layers when the Brush tool activates**

Find, inside the `m_brushToggle` toggled handler (around line 679):

```cpp
            if (m_layersDock) { m_layersDock->show(); m_layersDock->raise(); }
            refreshMaskPanel();
```

Replace with:

```cpp
            for (QDockWidget *d : {m_layersDock, m_tonePanelDock, m_colorPanelDock,
                                   m_toneCurveDock, m_layerLevelsDock,
                                   m_detailEffectsDock}) {
                if (d) { d->show(); d->raise(); }
            }
            refreshMaskPanel();
```

- [ ] **Step 13: Build to verify it all compiles**

Run: `cmake --build build`
Expected: build succeeds with no errors. This is the first build since Task 5's `LayersPanel` rewrite, so if `LayersPanel::maskAdjustChanged` or `setLevelsPreviewImage` are still referenced anywhere, this build will fail — grep the two names across `src/` if it does and remove any remaining call sites.

- [ ] **Step 14: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "feat: dock Tone/Color/Tone Curve/Levels/Detail & Effects as separate per-layer panels"
```

---

### Task 7: Manual smoke test

**Files:** none (verification only).

- [ ] **Step 1: Launch the app**

Run: `./build/nikontether`

- [ ] **Step 2: Open a photo and add a layer**

Open any photo, click the Layers dock's "Add" button (or press K to add a radial mask). Confirm the Tone, Color, Tone Curve, Levels, Detail & Effects, and Masks docks all become visible, tabified with Layers.

- [ ] **Step 3: Verify editing works and stays in sync**

Drag a slider in each of Tone, Color, Detail & Effects; edit the curve in Tone Curve; drag a Levels handle; change the mask shape in Masks. Confirm the image preview updates for each, and that switching the selected layer in the Layers list updates all six panels to that layer's values.

- [ ] **Step 4: Verify floating works independently**

Drag the Tone panel's title bar to float it away from the tab group. Confirm it keeps updating live as you switch layers, and other panels are unaffected.

- [ ] **Step 5: Verify Reset Panels**

Float/move a few panels around, then use View → Reset Panels. Confirm all six (Tone, Color, Tone Curve, Levels, Detail & Effects, Masks) return tabified with Layers.

- [ ] **Step 6: Verify persistence**

Float the Color panel to a different screen position, close the app, relaunch. Confirm its floated position is restored (per the existing panel-layout persistence feature).

- [ ] **Step 7: Report results**

No commit for this task — if any step fails, return to the relevant task above and fix before considering the plan complete.

---

## Self-Review Notes

- **Spec coverage:** all six panels (Tone, Color, Tone Curve, Levels, Detail & Effects, Masks) get independent docks (Tasks 1-4, 6); `LayersPanel` shrinks to list + Name/Opacity/Blend (Task 5); default tabify-with-Layers and Reset Panels (Task 6 Step 5); always-visible-when-layer-selected sync (Task 6 Step 7 `refreshMaskPanel`); persistence via existing `saveState`/`restoreState` and unique `setObjectName()`s (Task 6 Step 6); Orientation/global Adjustments dock untouched (no task touches `buildDock()`).
- **Placeholder scan:** none found — every step has complete code and exact file locations.
- **Type consistency:** `TonePanel::adjustChanged(int,int,int,int)`, `ColorPanel::adjustChanged(int,int,int,int)`, `DetailEffectsPanel::adjustChanged(int,int,int)`, `ToneCurvePanel::curveChanged(const QVector<QPointF>&)` are declared in Tasks 1-4 and consumed with matching signatures in Task 6 Step 6. `MaskAdjust` field names (`brightness`, `contrast`, `highlights`, `shadows`, `saturation`, `vibrance`, `temperature`, `tint`, `clarity`, `sharpen`, `vignette`, `curve`, `levels`) match `src/edit/Adjustments.h:58-79` exactly.
