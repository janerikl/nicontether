# Brush Tool + Foreground/Background Color Swatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Photoshop-style paintbrush to the Retouch tab, backed by a flippable foreground/background color swatch widget.

**Architecture:** The brush is a new `MaskType::Paint` layer kind, sibling to the existing `MaskType::Brush` mask — it reuses the exact same stroke-point storage (`Mask::stroke`), coverage rasterization (`rasterizeBrush`), and opacity/blend compositing already in `applyMasks()` (`src/edit/Adjustments.cpp`), with a flat `paintColor` fill instead of a tone adjustment as the layer's content. A new `ColorSwatchWidget` (fg/bg squares, swap, reset) lives in the main toolbar and feeds the current foreground color into new/active paint layers. No new pixel-buffer subsystem, no new `ImageCanvas` mouse-event code — the paint tool reuses the existing mask-mode canvas plumbing.

**Tech Stack:** C++17, Qt6 Widgets, CMake/CTest (plain-`assert` test executables, no test framework).

## Global Constraints

- No per-layer raw pixel buffer — paint layers are vector strokes + flat color, per `docs/superpowers/specs/2026-07-19-brush-tool-color-swatch-design.md`.
- `ColorSwatchWidget` default colors: foreground = black, background = white.
- Swap shortcut: `X`. Reset-to-default shortcut: `D`. Both active only while the retouch canvas has focus.
- Color picking uses Qt's built-in `QColorDialog` — no custom HSV picker.
- Brush options: Size, Hardness, Opacity — no clone stamp, no other blend modes beyond what `Mask`/`BlendMode` already supports (Normal is the default for new paint layers).
- Follow existing code style: manual `QSignalBlocker`-based mutual exclusion between left-toolbar tools (no `QActionGroup`); options rows added as pages to `m_toolOptionsStack`.

---

### Task 1: `MaskType::Paint` + `paintColor` field on `Mask`

**Files:**
- Modify: `src/edit/Adjustments.h:103` (enum), `src/edit/Adjustments.h:127-182` (`Mask` struct)

**Interfaces:**
- Produces: `MaskType::Paint` enumerator; `Mask::paintColor` (`QColor`, default `Qt::black`), included in `Mask::operator==`.

- [ ] **Step 1: Add the enumerator and field**

In `src/edit/Adjustments.h`, change line 103:
```cpp
enum class MaskType { Radial, Linear, Brush, Paint, None };
```

Add `#include <QColor>` to the include block near the top of the file (it currently only pulls `QColor` in transitively via `QImage`; make it explicit since we're adding a `QColor`-typed member):
```cpp
#include <QImage>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>
#include <QMetaType>
#include <cmath>
```

In the `Mask` struct, right after the `autoMask` field (after line 153, before `MaskAdjust adj;`), add:
```cpp
    // Paint: flat fill color for a MaskType::Paint layer. Composited using
    // the same `stroke`/`brushRadius`/`hardness` coverage as MaskType::Brush,
    // but the layer's content is a solid fill of this color instead of a
    // tone-adjusted copy of the image below (see applyMasks in
    // Adjustments.cpp). Unused by all other mask types.
    QColor paintColor = Qt::black;
```

Add `paintColor` to `Mask::operator==` (in the comparison chain around line 179), right after the `adj == o.adj` term:
```cpp
    bool operator==(const Mask &o) const {
        return name == o.name && visible == o.visible &&
               std::abs(opacity - o.opacity) < 1e-9 && blend == o.blend &&
               type == o.type && inverted == o.inverted &&
               std::abs(feather - o.feather) < 1e-9 && center == o.center &&
               std::abs(radiusX - o.radiusX) < 1e-9 &&
               std::abs(radiusY - o.radiusY) < 1e-9 &&
               std::abs(angle - o.angle) < 1e-9 && p0 == o.p0 && p1 == o.p1 &&
               stroke == o.stroke &&
               std::abs(brushRadius - o.brushRadius) < 1e-9 &&
               std::abs(hardness - o.hardness) < 1e-9 && autoMask == o.autoMask &&
               adj == o.adj && paintColor == o.paintColor &&
               sourceImagePath == o.sourceImagePath;
    }
```

- [ ] **Step 2: Build to confirm it compiles**

Run: `cmake --build build 2>&1 | tail -40` (adjust `build` to whatever the existing build directory is; if none exists yet, run `cmake -B build -S .` first).
Expected: compiles with no errors related to `Adjustments.h`. Existing `switch (t)`-style code elsewhere that enumerates `MaskType` without a `default:` case (e.g. `LayersPanel.cpp`'s `maskTypeLabel`, `RetouchWindow.cpp`'s `maskGlyph`) will now emit `-Wswitch` warnings for the unhandled `Paint` case — expected at this point; Task 3 and later fix each one as they're touched.

- [ ] **Step 3: Commit**

```bash
git add src/edit/Adjustments.h
git commit -m "feat: add MaskType::Paint and Mask::paintColor"
```

---

### Task 2: Compositing support for Paint layers + unit test

**Files:**
- Modify: `src/edit/Adjustments.cpp:280-326` (`applyMasks`), `src/edit/Adjustments.cpp:465-477` (`hasMaskEdits`)
- Create: `tests/AdjustmentsPaintTest.cpp`
- Modify: `CMakeLists.txt` (register the new test executable)

**Interfaces:**
- Consumes: `MaskType::Paint`, `Mask::paintColor` (Task 1).
- Produces: `applyMasks()` correctly composites Paint layers; verified by `adjustments_paint_test`.

- [ ] **Step 1: Write the failing test**

Create `tests/AdjustmentsPaintTest.cpp`:
```cpp
#include "edit/Adjustments.h"

#include <QImage>
#include <cassert>
#include <cstdio>

int main() {
    // A tiny black base image with one full-coverage red Paint layer should
    // come out red (opacity 1, Normal blend, hardness 1 covers the whole
    // frame from a single centered dab with a huge radius).
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(255, 0, 0);
        paint.brushRadius = 2.0; // width-normalized; radius = 2*W covers a 4x4 image entirely
        paint.hardness = 1.0;
        paint.opacity = 1.0;
        paint.blend = BlendMode::Normal;
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        QRgb center = out.pixel(2, 2);
        assert(qRed(center) > 250 && qGreen(center) < 5 && qBlue(center) < 5);
    }

    // An empty-stroke Paint layer contributes nothing (mirrors the existing
    // MaskType::Brush empty-stroke skip).
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(255, 0, 0);
        paint.opacity = 1.0;

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        QRgb center = out.pixel(2, 2);
        assert(qRed(center) == 0 && qGreen(center) == 0 && qBlue(center) == 0);
    }

    // Opacity 0.5 blends halfway between base and paint color.
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(200, 0, 0);
        paint.brushRadius = 2.0;
        paint.hardness = 1.0;
        paint.opacity = 0.5;
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        int r = qRed(out.pixel(2, 2));
        assert(r > 90 && r < 110); // ~100, halfway between 0 and 200
    }

    std::printf("AdjustmentsPaintTest: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Register the test in CMake and confirm it fails to build**

In `CMakeLists.txt`, after the existing `grid_overlay_test` block (after line 92), add:
```cmake
add_executable(adjustments_paint_test tests/AdjustmentsPaintTest.cpp src/edit/Adjustments.cpp)
target_include_directories(adjustments_paint_test PRIVATE src)
target_link_libraries(adjustments_paint_test PRIVATE Qt6::Widgets)
add_test(NAME adjustments_paint_test COMMAND adjustments_paint_test)
```

Run: `cmake -B build -S . && cmake --build build --target adjustments_paint_test 2>&1 | tail -40`
Expected: FAIL — assertions trip (Paint layers aren't composited yet, so all three sub-tests see the unchanged black base) or the build succeeds but `ctest -R adjustments_paint_test` fails. Confirm failure with: `ctest --test-dir build -R adjustments_paint_test --output-on-failure`.
Expected: assertion failure (first case: `center` is still black, not red).

- [ ] **Step 3: Implement Paint compositing in `applyMasks`**

In `src/edit/Adjustments.cpp`, `applyMasks()` (around line 280-326), make these changes:

Extend the empty-stroke skip check (line 291) to cover Paint layers too:
```cpp
        if ((m.type == MaskType::Brush || m.type == MaskType::Paint) && m.stroke.isEmpty()) continue;
```

Replace the `loc` computation (lines 292-294):
```cpp
        const QImage loc = imageLayer
                                ? applyLayerContent(coverFit(m.sourceImageCache, w, h), m.adj)
                                : applyLayerContent(img, m.adj);
```
with:
```cpp
        QImage loc;
        if (imageLayer) {
            loc = applyLayerContent(coverFit(m.sourceImageCache, w, h), m.adj);
        } else if (m.type == MaskType::Paint) {
            loc = QImage(w, h, QImage::Format_ARGB32);
            loc.fill(m.paintColor);
        } else {
            loc = applyLayerContent(img, m.adj);
        }
```

Extend the brush-coverage rasterization condition (line 295):
```cpp
        if (m.type == MaskType::Brush || m.type == MaskType::Paint) rasterizeBrush(m, cov, w, h, &img);
```

The `wgt` selection further down (lines 301-309) already falls through to the `cov[...]` branch via its trailing `else`, so `MaskType::Paint` is handled automatically there — no change needed.

- [ ] **Step 4: Extend `hasMaskEdits` to recognize Paint layers**

In `src/edit/Adjustments.cpp`, `hasMaskEdits()` (lines 465-477), the existing line:
```cpp
        if (m.type == MaskType::Brush && m.stroke.isEmpty()) continue;
```
already falls through to `return true` for any non-empty-stroke mask whose `m.adj.isZero()` — but Paint layers have `adj.isZero() == true` (they don't use `adj` at all), so they'd be incorrectly skipped by the `if (m.adj.isZero()) continue;` check above it. Add a Paint-specific branch before that check:
```cpp
        if (m.type == MaskType::Paint) {
            if (m.stroke.isEmpty()) continue;
            return true;
        }
        if (m.adj.isZero()) continue;
        if (m.type == MaskType::Brush && m.stroke.isEmpty()) continue;
        return true;
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --target adjustments_paint_test 2>&1 | tail -40 && ctest --test-dir build -R adjustments_paint_test --output-on-failure`
Expected: PASS — `AdjustmentsPaintTest: all assertions passed`.

- [ ] **Step 6: Commit**

```bash
git add src/edit/Adjustments.cpp tests/AdjustmentsPaintTest.cpp CMakeLists.txt
git commit -m "feat: composite Paint layers in applyMasks; add coverage test"
```

---

### Task 3: Label Paint layers in the Layers panel and mask-glyph switch

**Files:**
- Modify: `src/ui/LayersPanel.cpp:22-32` (`maskTypeLabel`)
- Modify: `src/edit/RetouchWindow.cpp` (find `maskGlyph(MaskType)` — the function referenced at `RetouchWindow.cpp:555`, `makeFlyoutToolIcon(maskGlyph(m_activeMaskSubtool))`)

**Interfaces:**
- Consumes: `MaskType::Paint` (Task 1).
- Produces: no unhandled-switch warnings; "Paint" label shown in the Layers list for paint layers.

- [ ] **Step 1: Update `maskTypeLabel`**

In `src/ui/LayersPanel.cpp`, change:
```cpp
QString maskTypeLabel(MaskType t) {
    switch (t) {
    case MaskType::Radial: return "Radial";
    case MaskType::Linear: return "Graduated";
    case MaskType::Brush:  return "Brush";
    case MaskType::None:   return "Layer";
    }
    return "Layer";
}
```
to:
```cpp
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
```

- [ ] **Step 2: Find and update `maskGlyph` in `RetouchWindow.cpp`**

Run: `grep -n "maskGlyph" src/edit/RetouchWindow.cpp`

Read the function body at the reported line, and add a `case MaskType::Paint:` arm. It draws an icon per mask type (mirroring `drawMaskRadial`/`drawMaskLinear`/`drawMaskBrush` referenced in `openMaskFlyout`'s `tools` vector) — reuse the existing `drawMaskBrush` glyph-drawing function for `Paint` (same visual language: a brush icon), since the Brush toolbar button is a separate top-level tool from the mask flyout and doesn't need a distinct glyph in this switch. Add:
```cpp
    case MaskType::Paint: return drawMaskBrush;
```
(or the equivalent expression pattern already used by the surrounding `case` arms — match whatever `maskGlyph`'s actual return type/expression style is once you've read it).

- [ ] **Step 3: Build**

Run: `cmake --build build 2>&1 | tail -40`
Expected: no `-Wswitch` warnings for `MaskType` in `LayersPanel.cpp` or `RetouchWindow.cpp`; build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/LayersPanel.cpp src/edit/RetouchWindow.cpp
git commit -m "feat: label and glyph support for Paint mask layers"
```

---

### Task 4: `RetouchTab::setPaintColor`

**Files:**
- Modify: `src/edit/RetouchTab.h:57-66` (public API block), `src/edit/RetouchTab.cpp` (near `setActiveMaskShape`, line 561)

**Interfaces:**
- Consumes: `Mask::paintColor` (Task 1), `RetouchTab::m_activeMask`, `m_adj.masks` (existing).
- Produces: `void RetouchTab::setPaintColor(const QColor &color)` — sets `paintColor` on the active mask if (and only if) it is a `MaskType::Paint` layer, then re-renders and marks the edit. Used by Task 8/9 wiring.

- [ ] **Step 1: Declare the method**

In `src/edit/RetouchTab.h`, add right after `setActiveMaskShape` (line 60):
```cpp
    void setActiveMaskShape(bool inverted, double feather, double hardness,
                            double brushRadius, bool autoMask);
    void setPaintColor(const QColor &color); // no-op unless the active layer is MaskType::Paint
```

Add `#include <QColor>` near the top of `RetouchTab.h` (it currently includes `<QWidget> <QImage> <QRect>`, none of which guarantee `QColor` directly — be explicit):
```cpp
#include <QWidget>
#include <QImage>
#include <QRect>
#include <QColor>
```

- [ ] **Step 2: Implement it**

In `src/edit/RetouchTab.cpp`, add right after `setActiveMaskShape`'s body (after line 574):
```cpp
void RetouchTab::setPaintColor(const QColor &color) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (m.type != MaskType::Paint) return;
    m.paintColor = color;
    retone();
    markEdited();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build 2>&1 | tail -40`
Expected: compiles cleanly.

- [ ] **Step 4: Commit**

```bash
git add src/edit/RetouchTab.h src/edit/RetouchTab.cpp
git commit -m "feat: RetouchTab::setPaintColor for the active Paint layer"
```

---

### Task 5: `ColorSwatchWidget` (fg/bg squares, swap, reset)

**Files:**
- Create: `src/ui/ColorSwatchWidget.h`
- Create: `src/ui/ColorSwatchWidget.cpp`
- Modify: `CMakeLists.txt` (add the new source to the `nikontether` target's source list)

**Interfaces:**
- Produces:
  ```cpp
  class ColorSwatchWidget : public QWidget {
      Q_OBJECT
  public:
      explicit ColorSwatchWidget(QWidget *parent = nullptr);
      QColor foregroundColor() const;
      QColor backgroundColor() const;
  public slots:
      void swapColors();
      void resetColors(); // fg=black, bg=white
  signals:
      void foregroundColorChanged(const QColor &color);
      void backgroundColorChanged(const QColor &color);
  };
  ```
  Consumed by Task 6 (toolbar embedding + shortcuts) and Task 8/9 (feeding paint color).

- [ ] **Step 1: Write the header**

Create `src/ui/ColorSwatchWidget.h`:
```cpp
#pragma once
#include <QWidget>
#include <QColor>

// Photoshop-style foreground/background color swatch: two overlapping
// squares (fg front, bg back), a swap arrow, and a reset-to-default icon.
// Click a square to open QColorDialog and change that color.
class ColorSwatchWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatchWidget(QWidget *parent = nullptr);

    QColor foregroundColor() const { return m_fg; }
    QColor backgroundColor() const { return m_bg; }

    QSize sizeHint() const override { return QSize(36, 36); }

public slots:
    void swapColors();
    void resetColors();

signals:
    void foregroundColorChanged(const QColor &color);
    void backgroundColorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect fgRect() const;
    QRect bgRect() const;
    QRect swapRect() const;
    QRect resetRect() const;

    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;
};
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/ColorSwatchWidget.cpp`:
```cpp
#include "ui/ColorSwatchWidget.h"

#include <QColorDialog>
#include <QMouseEvent>
#include <QPainter>

ColorSwatchWidget::ColorSwatchWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(36, 36);
    setToolTip("Foreground/Background color — click a square to change it, "
              "X to swap, D to reset to black/white");
}

QRect ColorSwatchWidget::fgRect() const { return QRect(0, 0, 22, 22); }
QRect ColorSwatchWidget::bgRect() const { return QRect(12, 12, 22, 22); }
QRect ColorSwatchWidget::swapRect() const { return QRect(24, 0, 12, 12); }
QRect ColorSwatchWidget::resetRect() const { return QRect(0, 24, 12, 12); }

void ColorSwatchWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Background square (drawn first so the foreground square overlaps it).
    p.fillRect(bgRect(), m_bg);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(bgRect().adjusted(0, 0, -1, -1));

    // Foreground square.
    p.fillRect(fgRect(), m_fg);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(fgRect().adjusted(0, 0, -1, -1));

    // Swap arrow (small curved arrow glyph, top-right).
    p.setPen(QColor(200, 200, 200));
    QRect sr = swapRect();
    p.drawArc(sr.adjusted(1, 1, -1, -1), 30 * 16, 300 * 16);

    // Reset-to-default icon (tiny black/white squares, bottom-left).
    QRect rr = resetRect();
    p.fillRect(QRect(rr.left(), rr.top(), 6, 6), Qt::white);
    p.fillRect(QRect(rr.left() + 3, rr.top() + 3, 6, 6), Qt::black);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(QRect(rr.left(), rr.top(), 6, 6));
    p.drawRect(QRect(rr.left() + 3, rr.top() + 3, 6, 6));
}

void ColorSwatchWidget::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;
    const QPoint pos = ev->pos();

    if (swapRect().contains(pos)) {
        swapColors();
        return;
    }
    if (resetRect().contains(pos)) {
        resetColors();
        return;
    }
    if (fgRect().contains(pos)) {
        QColor c = QColorDialog::getColor(m_fg, this, "Foreground Color");
        if (c.isValid()) {
            m_fg = c;
            update();
            emit foregroundColorChanged(m_fg);
        }
        return;
    }
    if (bgRect().contains(pos)) {
        QColor c = QColorDialog::getColor(m_bg, this, "Background Color");
        if (c.isValid()) {
            m_bg = c;
            update();
            emit backgroundColorChanged(m_bg);
        }
        return;
    }
}

void ColorSwatchWidget::swapColors() {
    std::swap(m_fg, m_bg);
    update();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}

void ColorSwatchWidget::resetColors() {
    m_fg = Qt::black;
    m_bg = Qt::white;
    update();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}
```

- [ ] **Step 3: Register the new source in CMake**

In `CMakeLists.txt`, add `src/ui/ColorSwatchWidget.cpp` to the `nikontether` target's source list (line 19-48), alongside the other `src/ui/*.cpp` entries (e.g. right after `src/ui/LayersPanel.cpp`):
```cmake
    src/ui/LayersPanel.cpp
    src/ui/ColorSwatchWidget.cpp
    src/ui/ToolFlyout.cpp
```

- [ ] **Step 4: Build**

Run: `cmake --build build 2>&1 | tail -40`
Expected: compiles cleanly (this widget isn't wired into the UI yet, so nothing runtime-testable until Task 6 — this step only confirms it compiles standalone).

- [ ] **Step 5: Commit**

```bash
git add src/ui/ColorSwatchWidget.h src/ui/ColorSwatchWidget.cpp CMakeLists.txt
git commit -m "feat: add ColorSwatchWidget (fg/bg squares, swap, reset)"
```

---

### Task 6: Embed the swatch in the toolbar with X/D shortcuts

**Files:**
- Modify: `src/edit/RetouchWindow.h` (add a member + includes)
- Modify: `src/edit/RetouchWindow.cpp` (`buildToolPanel()`, around line 521-561)

**Interfaces:**
- Consumes: `ColorSwatchWidget` (Task 5).
- Produces: `m_colorSwatch` member on `RetouchWindow`, visible in the main toolbar; `X`/`D` global shortcuts routed to it while a retouch tab is open.

- [ ] **Step 1: Add the member**

In `src/edit/RetouchWindow.h`, add `#include "ui/ColorSwatchWidget.h"` to the includes, and add `ColorSwatchWidget *m_colorSwatch = nullptr;` alongside the other tool-panel member declarations (near `QToolButton *m_healToggle`, etc.).

- [ ] **Step 2: Add it to the toolbar**

In `src/edit/RetouchWindow.cpp`, `buildToolPanel()`, right after the `m_maskToggle` block (after line 561, before the mutual-exclusion `connect` calls begin), add:
```cpp
    m_colorSwatch = new ColorSwatchWidget;
    m_toolsBar->addWidget(m_colorSwatch);
```

- [ ] **Step 3: Wire X/D shortcuts**

Still in `buildToolPanel()`, after the widget is added, wire two `QShortcut`s scoped to the window (so they fire regardless of which child widget has focus, matching how `Qt::Key_H` etc. work as `QToolButton` shortcuts):
```cpp
    auto *swapShortcut = new QShortcut(QKeySequence(Qt::Key_X), this);
    connect(swapShortcut, &QShortcut::activated, m_colorSwatch, &ColorSwatchWidget::swapColors);
    auto *resetShortcut = new QShortcut(QKeySequence(Qt::Key_D), this);
    connect(resetShortcut, &QShortcut::activated, m_colorSwatch, &ColorSwatchWidget::resetColors);
```
Add `#include <QShortcut>` to `RetouchWindow.cpp`'s includes if not already present (`grep -n "#include <QShortcut>" src/edit/RetouchWindow.cpp` to check first).

- [ ] **Step 4: Build and manually verify**

Run: `cmake --build build 2>&1 | tail -40 && ./build/nikontether` (adjust the binary path/name to match the actual build output if different — check with `find build -maxdepth 1 -type f -executable`).

Manually verify in the running app: open a photo in Retouch, confirm the swatch widget appears in the left toolbar (two overlapping black/white squares), click the front square to open a color dialog and pick a color, confirm it updates; press `X` and confirm fg/bg swap; press `D` and confirm it resets to black/white.

- [ ] **Step 5: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "feat: embed ColorSwatchWidget in the Retouch toolbar with X/D shortcuts"
```

---

### Task 7: Extend ctrl+wheel brush resize to Paint layers

**Files:**
- Modify: `src/edit/ImageCanvas.cpp` (the `wheelEvent` ctrl+wheel brush-resize branch, around line 686-694)

**Interfaces:**
- Consumes: `MaskType::Paint` (Task 1).
- Produces: ctrl+wheel over the canvas resizes the brush the same way it already does for `MaskType::Brush`, when a Paint layer is active.

- [ ] **Step 1: Extend the condition**

In `src/edit/ImageCanvas.cpp`, change:
```cpp
    if (m_maskMode && m_maskKind == MaskType::Brush) {
```
to:
```cpp
    if (m_maskMode && (m_maskKind == MaskType::Brush || m_maskKind == MaskType::Paint)) {
```

- [ ] **Step 2: Build**

Run: `cmake --build build 2>&1 | tail -40`
Expected: compiles cleanly. (Runtime verification happens in Task 8's manual test, once the Brush tool can actually activate `MaskType::Paint` mode.)

- [ ] **Step 3: Commit**

```bash
git add src/edit/ImageCanvas.cpp
git commit -m "feat: ctrl+wheel brush resize also applies to Paint layers"
```

---

### Task 8: Brush toolbar button, options row, and layer creation

**Files:**
- Modify: `src/edit/RetouchWindow.h` (add `m_brushToggle`, size/hardness/opacity slider members)
- Modify: `src/edit/RetouchWindow.cpp` (`buildToolPanel()` and `buildToolOptionsBar()`)

**Interfaces:**
- Consumes: `RetouchTab::addMask(MaskType)`, `RetouchTab::setActiveMaskShape(...)`, `RetouchTab::setPaintColor(QColor)` (Task 4), `RetouchTab::setMaskMode(bool)`, `ColorSwatchWidget::foregroundColor()` (Task 5/6).
- Produces: a "Brush" tool button mutually exclusive with Zoom/Crop/Heal/Mask, whose activation creates a new `MaskType::Paint` layer using the current foreground color, with a Size/Hardness/Opacity options row.

- [ ] **Step 1: Declare new members**

In `src/edit/RetouchWindow.h`, add near the other tool members:
```cpp
    QToolButton *m_brushToggle = nullptr;
    QSlider *m_paintSize = nullptr;
    QSlider *m_paintHardness = nullptr;
    QSlider *m_paintOpacity = nullptr;
```

- [ ] **Step 2: Add the toolbar button**

In `src/edit/RetouchWindow.cpp`, `buildToolPanel()`, right after the `m_healToggle` block (after line 552, before `m_maskToggle`'s block):
```cpp
    m_brushToggle = new QToolButton;
    m_brushToggle->setIcon(makeHealIcon()); // TODO placeholder glyph — reuse an existing brush-shaped icon
    m_brushToggle->setCheckable(true);
    m_brushToggle->setShortcut(QKeySequence(Qt::Key_B));
    m_brushToggle->setToolTip("Brush (B) — paint with the foreground color; Ctrl+wheel resizes brush");
    m_toolsBar->addWidget(m_brushToggle);
```
Replace the placeholder icon: reuse `drawMaskBrush` via the same `makeToolIcon(draw)` helper used for `makeHealIcon`/`makeCropIcon` (defined near line 92-197, per the earlier icon-helpers investigation). Add a sibling function next to `makeHealIcon()`:
```cpp
QIcon makeBrushIcon() { return makeToolIcon(drawMaskBrush); }
```
and use `m_brushToggle->setIcon(makeBrushIcon());` instead of the `makeHealIcon()` placeholder above.

- [ ] **Step 3: Wire mutual exclusion — update every existing tool's on-handler**

In each of the five existing `toggled` lambdas in `buildToolPanel()` (`m_toolZoom`, `m_cropToggle`, `m_healToggle`, `m_maskToggle`, plus `m_wbPick` if it has its own — check for it), add `m_brushToggle` to the signal-blocker/uncheck list and `tab->setMaskMode(false)` if not already present. For example, `m_toolZoom`'s handler becomes:
```cpp
    connect(m_toolZoom, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            { QSignalBlocker b(m_brushToggle); m_brushToggle->setChecked(false); }
            if (tab) { tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); tab->setMaskMode(false); }
            m_toolOptionsStack->setCurrentIndex(0);
            m_toolOptionsBar->setVisible(true);
        } else {
            m_toolOptionsBar->setVisible(false);
        }
        if (tab && tab->isReady()) tab->setZoomMode(on);
    });
```
Apply the same `{ QSignalBlocker b(m_brushToggle); m_brushToggle->setChecked(false); }` addition to the `on` branches of `m_cropToggle`, `m_healToggle`, and `m_maskToggle`'s handlers.

- [ ] **Step 4: Add the Brush toggle handler**

After the `m_maskToggle` handler (after line 634), add:
```cpp
    connect(m_brushToggle, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); }
            m_toolOptionsStack->setCurrentIndex(3);
            m_toolOptionsBar->setVisible(true);
            if (tab && tab->isReady()) {
                tab->addMask(MaskType::Paint);
                tab->setPaintColor(m_colorSwatch->foregroundColor());
                tab->setActiveMaskShape(false, 0.0, m_paintHardness->value() / 100.0,
                                        m_paintSize->value() / 100.0, false);
                tab->setActiveMaskOpacity(m_paintOpacity->value() / 100.0);
            }
            if (m_layersDock) { m_layersDock->show(); m_layersDock->raise(); }
            refreshMaskPanel();
        } else {
            m_toolOptionsBar->setVisible(false);
            if (tab && tab->isReady()) tab->setMaskMode(false);
        }
    });
```
`m_paintSize` uses the same convention as `MaskPanel::m_brushSize` (`src/ui/MaskPanel.cpp:41`): an integer slider representing brush radius as a percentage of image width, range 1–40, converted via `value() / 100.0` into the width-normalized `Mask::brushRadius` field.

- [ ] **Step 5: Add the options row**

In `buildToolOptionsBar()`, after the existing heal page (`m_toolOptionsStack->addWidget(healPage)` and its two `connect` calls, i.e. after the block ending around line 639+ in the heal section), add page index 3:
```cpp
    // --- Brush page (index 3) ---
    auto *brushPage = new QWidget;
    auto *brushRow = new QHBoxLayout(brushPage);
    brushRow->setContentsMargins(4, 2, 4, 2);
    m_paintSize = new QSlider(Qt::Horizontal);
    m_paintSize->setRange(1, 40); // percent of image width, same scale as MaskPanel::m_brushSize
    m_paintSize->setValue(6);
    m_paintSize->setMinimumWidth(120);
    m_paintHardness = new QSlider(Qt::Horizontal);
    m_paintHardness->setRange(0, 100);
    m_paintHardness->setValue(100);
    m_paintHardness->setMinimumWidth(100);
    m_paintOpacity = new QSlider(Qt::Horizontal);
    m_paintOpacity->setRange(1, 100);
    m_paintOpacity->setValue(100);
    m_paintOpacity->setMinimumWidth(100);
    brushRow->addWidget(new QLabel("Size:"));
    brushRow->addWidget(m_paintSize);
    brushRow->addWidget(new QLabel("Hardness:"));
    brushRow->addWidget(m_paintHardness);
    brushRow->addWidget(new QLabel("Opacity:"));
    brushRow->addWidget(m_paintOpacity);
    brushRow->addStretch(1);
    m_toolOptionsStack->addWidget(brushPage);

    connect(m_paintSize, &QSlider::valueChanged, this, [this](int v) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setActiveMaskShape(false, 0.0, m_paintHardness->value() / 100.0,
                                         v / 100.0, false);
    });
    connect(m_paintHardness, &QSlider::valueChanged, this, [this](int v) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setActiveMaskShape(false, 0.0, v / 100.0,
                                         m_paintSize->value() / 100.0, false);
    });
    connect(m_paintOpacity, &QSlider::valueChanged, this, [this](int v) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setActiveMaskOpacity(v / 100.0);
    });
```

- [ ] **Step 6: Build and manually verify**

Run: `cmake --build build 2>&1 | tail -40 && ./build/nikontether`

Manually verify: open a photo in Retouch, pick a foreground color in the swatch, click the Brush tool (or press `B`), confirm a new "Paint" layer appears in the Layers panel; drag on the canvas and confirm strokes render in the chosen color; adjust Size/Hardness/Opacity sliders and confirm strokes change accordingly; ctrl+wheel over the canvas resizes the brush cursor; release the mouse and press Ctrl+Z, confirming the whole stroke undoes in one step; confirm the paint layer can be hidden/reordered/deleted/opacity-changed/blend-changed from the Layers panel like any other layer.

- [ ] **Step 7: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "feat: add Brush tool (creates a Paint layer, Size/Hardness/Opacity controls)"
```

---

### Task 9: Live foreground-color updates while the Brush tool is active

**Files:**
- Modify: `src/edit/RetouchWindow.cpp` (`buildToolPanel()`, near the `m_colorSwatch` setup from Task 6)

**Interfaces:**
- Consumes: `ColorSwatchWidget::foregroundColorChanged(const QColor&)` (Task 5), `RetouchTab::setPaintColor(QColor)` (Task 4).
- Produces: picking a new foreground color while a Paint layer is active immediately recolors that layer (it doesn't retroactively affect other, previously-created paint layers, since `setPaintColor` only touches `m_activeMask`).

- [ ] **Step 1: Wire the signal**

In `buildToolPanel()`, right after the `m_colorSwatch` construction from Task 6 Step 2, add:
```cpp
    connect(m_colorSwatch, &ColorSwatchWidget::foregroundColorChanged, this,
            [this](const QColor &c) {
                RetouchTab *tab = currentTab();
                if (tab && tab->isReady()) tab->setPaintColor(c);
            });
```
(`RetouchTab::setPaintColor` is already a no-op unless the active layer is `MaskType::Paint`, per Task 4 — so this is safe to connect unconditionally regardless of which tool is currently active.)

- [ ] **Step 2: Build and manually verify**

Run: `cmake --build build 2>&1 | tail -40 && ./build/nikontether`

Manually verify: with the Brush tool active and a Paint layer selected, change the foreground color via the swatch and confirm the active layer's already-painted strokes recolor immediately; switch to a different (non-paint) tool and change the foreground color, confirming nothing errors and no unrelated layer changes.

- [ ] **Step 3: Commit**

```bash
git add src/edit/RetouchWindow.cpp
git commit -m "feat: live-recolor the active Paint layer when the foreground color changes"
```

---

## Self-Review Notes

- **Spec coverage:** ColorSwatchWidget (fg/bg squares, swap, reset, defaults, shortcuts) → Tasks 5-6. Brush paint-onto-layer via vector strokes + flat fill → Tasks 1-2, 8. Options bar (Size/Hardness/Opacity) → Task 8. Ctrl+wheel resize → Task 7. Undo coalescing → covered for free by existing `markEdited`/`onMaskEditFinished` debounce (no new code needed; verified manually in Task 8 Step 6). Layers-panel integration (label, visibility, reorder, delete, opacity, blend) → Task 3 plus existing generic layer UI (no new code required — `LayersPanel` already handles any `Mask` regardless of type).
- **Placeholder scan:** Task 8 Step 2 contains one explicit `// TODO placeholder glyph` that Step 2 itself immediately resolves in the same task (not a dangling TODO left for later) — the icon is swapped to `makeBrushIcon()`/`drawMaskBrush` before the task's build-and-verify step, so no unresolved placeholder ships.
- **Type consistency:** `Mask::paintColor` (Task 1) → `RetouchTab::setPaintColor(const QColor&)` (Task 4) → `ColorSwatchWidget::foregroundColor()` (Task 5) → wired in Tasks 8/9. `MaskType::Paint` used consistently across Tasks 1, 2, 3, 7, 8. Brush-size normalization (slider value ÷ 100.0 → `Mask::brushRadius`, range 1-40) is verified against `MaskPanel::m_brushSize`'s actual convention (`src/ui/MaskPanel.cpp:41-42,64-66`) and used consistently in Task 8 Steps 4 and 5.
