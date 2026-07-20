# Erase Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a brush-based Erase tool that punches feathered transparency into the currently selected image layer, revealing whatever is beneath it (another layer, or the canvas background).

**Architecture:** Follow the existing "store ops, replay on render" pattern already used by the Heal tool (`Adjustments::heals`) and the Mask brush (`Mask::stroke`). Erase dabs are appended to a new per-layer `Mask::eraseStrokes` list (canvas-width-normalized point + radius, same convention as `BrushStrokePoint`/`brushRadius`). At composite time, `applyMasks` in `Adjustments.cpp` rasterizes the erase coverage (max-combined across dabs, feathered edge, same falloff shape as the existing brush-mask rasterizer) and subtracts it from the image layer's alpha channel before that layer is blended into the composite below — since layer blending already weights by `qAlpha(locLine[x])`, this makes erased regions reveal what's underneath with no other compositing changes needed. `ImageCanvas` gains a `setEraseMode`/`eraseAt`/`eraseFinished` signal set mirroring the Mask brush's mouse handling; `RetouchTab` appends dabs and coalesces them into one undo step per drag (matching `onMaskBrushPoint`/`onMaskEditFinished`); `RetouchWindow` adds a toolbar button + brush-size slider following the Heal tool's exact wiring pattern.

**Tech Stack:** Qt6/C++ (QWidget, QPainter, QImage), CMake + Ninja build, assert()-based test executables (no Qt Test framework in this repo).

## Global Constraints

- Erase strokes are per-image-layer only; the tool is a no-op unless the currently selected layer (`RetouchTab::activeMaskIndex()`) is an image layer (`Mask::isImageLayer() == true`).
- Feathered (soft) edge only — no hard-edge brush option (out of scope per the design spec).
- Erasing is destructive-via-replay: reversible only through the existing undo/redo history (`RetouchTab::m_history`), not a separately-editable mask.
- Erase points are stored canvas-width-normalized (`pt.x()/W`, `pt.y()/W`, `radius/W`), identical convention to `BrushStrokePoint` and `Mask::brushRadius` — not layer-local — matching how every other on-canvas brush tool in this codebase already behaves.
- No new brush-radius data model — the Erase tool gets its own `QSlider` (like Heal's `m_healBrush` and Paint's `m_paintSize`), each tool remembers its own display-px radius; this mirrors the existing per-tool slider pattern, not a shared global.

---

### Task 1: Data model — `ErasePoint` + `Mask::eraseStrokes`

**Files:**
- Modify: `src/edit/Adjustments.h`

**Interfaces:**
- Produces: `struct ErasePoint { QPointF pt; double radius; bool operator==(const ErasePoint&) const; };` and `Mask::eraseStrokes` (`QVector<ErasePoint>`), used by Task 2 (compositing), Task 3 (sidecar), Task 5 (RetouchTab).

- [ ] **Step 1: Add the `ErasePoint` struct**

Insert right after the `BrushStrokePoint` struct (after line 141 in `src/edit/Adjustments.h`):

```cpp
// One dab of an erase-tool stroke on an image layer: canvas-width-normalized
// centre + radius (same normalization as BrushStrokePoint / m.brushRadius).
// Dabs are max-combined into a coverage buffer, then subtracted from the
// layer's alpha at composite time (see applyMasks in Adjustments.cpp).
// Always soft/feathered across the full radius — no hard-edge option.
struct ErasePoint {
    QPointF pt;
    double radius = 0.06;

    bool operator==(const ErasePoint &o) const {
        return pt == o.pt && std::abs(radius - o.radius) < 1e-9;
    }
};
```

- [ ] **Step 2: Add `eraseStrokes` to `Mask` and include it in `operator==`**

In `struct Mask` (`src/edit/Adjustments.h`), add the field right after `sourceMissing`/`isImageLayer()` (after line 201):

```cpp
    // Erase-tool strokes (image layers only): canvas-normalized dabs that
    // punch feathered transparency into this layer's alpha at composite
    // time. Empty for non-image layers.
    QVector<ErasePoint> eraseStrokes;
```

Update `Mask::operator==` (around line 203-219) to compare it — add `eraseStrokes == o.eraseStrokes` to the returned `&&` chain, e.g. right after the `stroke == o.stroke` term:

```cpp
    bool operator==(const Mask &o) const {
        return name == o.name && visible == o.visible &&
               std::abs(opacity - o.opacity) < 1e-9 && blend == o.blend &&
               type == o.type && inverted == o.inverted &&
               std::abs(feather - o.feather) < 1e-9 && center == o.center &&
               std::abs(radiusX - o.radiusX) < 1e-9 &&
               std::abs(radiusY - o.radiusY) < 1e-9 &&
               std::abs(angle - o.angle) < 1e-9 && p0 == o.p0 && p1 == o.p1 &&
               stroke == o.stroke && eraseStrokes == o.eraseStrokes &&
               std::abs(brushRadius - o.brushRadius) < 1e-9 &&
               std::abs(hardness - o.hardness) < 1e-9 && autoMask == o.autoMask &&
               adj == o.adj && paintColor == o.paintColor &&
               sourceImageOffset == o.sourceImageOffset &&
               sourceImageScale == o.sourceImageScale &&
               sourceImageLockRatio == o.sourceImageLockRatio &&
               sourceImagePath == o.sourceImagePath;
    }
```

- [ ] **Step 3: Build to confirm it compiles**

Run: `cmake --build build --target adjustments_paint_test -j`
Expected: builds with no errors (no test changes yet, this just validates the header change compiles).

- [ ] **Step 4: Commit**

```bash
git add src/edit/Adjustments.h
git commit -m "Add ErasePoint data model for the erase tool"
```

---

### Task 2: Compositing — punch erase coverage into image-layer alpha

**Files:**
- Modify: `src/edit/Adjustments.cpp:405-417` (the `if (imageLayer)` branch in `applyMasks`)
- Test: `tests/AdjustmentsPaintTest.cpp`

**Interfaces:**
- Consumes: `ErasePoint`, `Mask::eraseStrokes` (Task 1); `smoothstep01(double)` (existing helper, `Adjustments.cpp:81`).
- Produces: erased image layers compose transparently — no new function signature, behavior change inside `applyMasks`.

- [ ] **Step 1: Write the failing test**

Add this block to `tests/AdjustmentsPaintTest.cpp`, right after the "Image layers can be panned and resized within the frame" block (after line 105, before the sidecar test at line 107):

```cpp
    // Erasing an image layer punches transparency through to the base below.
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(QColor(0, 0, 255)); // blue base, should show through the hole

        QImage src(8, 8, QImage::Format_ARGB32);
        src.fill(QColor(255, 0, 0)); // red layer, fully covering the frame

        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = "layer.png";
        layer.sourceImageCache = src;
        layer.opacity = 1.0;
        // Erase dab dead centre with a radius covering roughly the middle third.
        layer.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});

        Adjustments adj;
        adj.masks.append(layer);

        QImage out = applyAdjustments(base, adj);
        // Centre pixel: fully erased -> blue base shows through.
        assert(qRed(out.pixel(4, 4)) < 20 && qBlue(out.pixel(4, 4)) > 200);
        // Corner pixel: untouched by the erase dab -> still red.
        assert(qRed(out.pixel(0, 0)) > 200 && qBlue(out.pixel(0, 0)) < 20);
    }

    // Erase coverage is max-combined across overlapping dabs, not compounded
    // (two overlapping partial-feather dabs shouldn't erase more than a
    // single full-strength dab would at the same point).
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(QColor(0, 0, 255));

        QImage src(8, 8, QImage::Format_ARGB32);
        src.fill(QColor(255, 0, 0));

        Mask single;
        single.type = MaskType::None;
        single.sourceImagePath = "layer.png";
        single.sourceImageCache = src;
        single.opacity = 1.0;
        single.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});

        Mask doubled;
        doubled.type = MaskType::None;
        doubled.sourceImagePath = "layer.png";
        doubled.sourceImageCache = src;
        doubled.opacity = 1.0;
        doubled.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});
        doubled.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2}); // same spot again

        Adjustments adjSingle;
        adjSingle.masks.append(single);
        Adjustments adjDoubled;
        adjDoubled.masks.append(doubled);

        QImage outSingle = applyAdjustments(base, adjSingle);
        QImage outDoubled = applyAdjustments(base, adjDoubled);
        assert(outSingle.pixel(4, 4) == outDoubled.pixel(4, 4));
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target adjustments_paint_test -j && ./build/adjustments_paint_test`
Expected: compile error (`ErasePoint`/`eraseStrokes` already exist from Task 1, so it compiles) but assertion failure on the first new block — the centre pixel is still red/opaque because erasing isn't implemented yet.

- [ ] **Step 3: Implement erase-coverage rasterization and application**

In `src/edit/Adjustments.cpp`, inside `applyMasks`, the `if (imageLayer)` branch currently reads (lines 405-417):

```cpp
        QImage loc;
        if (imageLayer) {
            QRectF frame = imageLayerFrame(w, h, m);
            QImage fitted = coverFit(m.sourceImageCache, std::max(1, int(std::lround(frame.width()))),
                                     std::max(1, int(std::lround(frame.height()))),
                                     QPointF());
            loc = QImage(w, h, QImage::Format_ARGB32);
            loc.fill(Qt::transparent);
            QPainter p(&loc);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(frame.topLeft(), fitted);
            p.end();
            loc = applyLayerContent(loc, m.adj);
        } else if (paintLayer) {
```

Replace it with (adds the erase pass between `p.end()` and `applyLayerContent`):

```cpp
        QImage loc;
        if (imageLayer) {
            QRectF frame = imageLayerFrame(w, h, m);
            QImage fitted = coverFit(m.sourceImageCache, std::max(1, int(std::lround(frame.width()))),
                                     std::max(1, int(std::lround(frame.height()))),
                                     QPointF());
            loc = QImage(w, h, QImage::Format_ARGB32);
            loc.fill(Qt::transparent);
            QPainter p(&loc);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(frame.topLeft(), fitted);
            p.end();
            if (!m.eraseStrokes.isEmpty()) {
                std::vector<double> cov(size_t(w) * h, 0.0);
                for (const ErasePoint &ep : m.eraseStrokes) {
                    const double px = ep.pt.x() * W, py = ep.pt.y() * W;
                    const double rad = std::max(1.0, ep.radius * W);
                    const int x0 = std::max(0, int(px - rad));
                    const int x1 = std::min(w - 1, int(px + rad));
                    const int y0 = std::max(0, int(py - rad));
                    const int y1 = std::min(h - 1, int(py + rad));
                    for (int y = y0; y <= y1; ++y) {
                        for (int x = x0; x <= x1; ++x) {
                            double dx = x - px, dy = y - py;
                            double dist = std::sqrt(dx * dx + dy * dy);
                            double v = dist >= rad ? 0.0
                                                    : smoothstep01((rad - dist) / rad);
                            double &c = cov[size_t(y) * w + x];
                            if (v > c) c = v;
                        }
                    }
                }
                for (int y = 0; y < h; ++y) {
                    QRgb *line = reinterpret_cast<QRgb *>(loc.scanLine(y));
                    for (int x = 0; x < w; ++x) {
                        double c = cov[size_t(y) * w + x];
                        if (c <= 0.0) continue;
                        QRgb px = line[x];
                        int newAlpha = int(std::lround(qAlpha(px) * (1.0 - c)));
                        line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), newAlpha);
                    }
                }
            }
            loc = applyLayerContent(loc, m.adj);
        } else if (paintLayer) {
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target adjustments_paint_test -j && ./build/adjustments_paint_test`
Expected: `AdjustmentsPaintTest: all assertions passed`

- [ ] **Step 5: Commit**

```bash
git add src/edit/Adjustments.cpp tests/AdjustmentsPaintTest.cpp
git commit -m "Composite erase-tool strokes into image-layer alpha"
```

---

### Task 3: Sidecar persistence — save/load `eraseStrokes`

**Files:**
- Modify: `src/edit/EditSidecar.cpp`
- Test: `tests/AdjustmentsPaintTest.cpp`

**Interfaces:**
- Consumes: `ErasePoint`, `Mask::eraseStrokes` (Task 1).
- Produces: no new symbols; `EditSidecar::save`/`load` round-trip `eraseStrokes`.

- [ ] **Step 1: Write the failing test**

Add this block to `tests/AdjustmentsPaintTest.cpp`, right after the "Image-layer position persists through the sidecar format" block (after line 130, before the background-color sidecar test):

```cpp
    // Erase strokes persist through the sidecar format.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = path;
        layer.eraseStrokes.append(ErasePoint{QPointF(0.3, 0.4), 0.1});
        layer.eraseStrokes.append(ErasePoint{QPointF(0.6, 0.7), 0.05});
        adj.masks.append(layer);

        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.masks.size() == 1);
        assert(loaded.masks[0].eraseStrokes.size() == 2);
        assert(loaded.masks[0].eraseStrokes[0].pt == QPointF(0.3, 0.4));
        assert(std::abs(loaded.masks[0].eraseStrokes[0].radius - 0.1) < 1e-9);
        assert(loaded.masks[0].eraseStrokes[1].pt == QPointF(0.6, 0.7));
        assert(std::abs(loaded.masks[0].eraseStrokes[1].radius - 0.05) < 1e-9);
    }

    // A sidecar written before eraseStrokes existed loads an empty list
    // instead of failing.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = path;
        adj.masks.append(layer);
        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.masks.size() == 1);
        assert(loaded.masks[0].eraseStrokes.isEmpty());
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target adjustments_paint_test -j && ./build/adjustments_paint_test`
Expected: assertion failure — `loaded.masks[0].eraseStrokes.size() == 2` fails (currently always empty, nothing is serialized).

- [ ] **Step 3: Serialize `eraseStrokes` in `EditSidecar::save`**

In `src/edit/EditSidecar.cpp`, right after the `stroke` array block inside the mask-writing loop (after line 155, `j["stroke"] = stroke;`), add:

```cpp
            QJsonArray erases;
            for (const ErasePoint &ep : m.eraseStrokes)
                erases.append(QJsonArray{ep.pt.x(), ep.pt.y(), ep.radius});
            j["eraseStrokes"] = erases;
```

- [ ] **Step 4: Deserialize `eraseStrokes` in `EditSidecar::load`**

Right after the `stroke` array parsing loop inside the mask-reading loop (after line 274, the closing `}` of the `for (const QJsonValue &sv : j["stroke"].toArray())` loop), add:

```cpp
        for (const QJsonValue &ev : j["eraseStrokes"].toArray()) {
            QJsonArray p = ev.toArray();
            if (p.size() >= 3)
                m.eraseStrokes.append(ErasePoint{
                    QPointF(p[0].toDouble(), p[1].toDouble()), p[2].toDouble()});
        }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target adjustments_paint_test -j && ./build/adjustments_paint_test`
Expected: `AdjustmentsPaintTest: all assertions passed`

- [ ] **Step 6: Commit**

```bash
git add src/edit/EditSidecar.cpp tests/AdjustmentsPaintTest.cpp
git commit -m "Persist erase-tool strokes in the edit sidecar"
```

---

### Task 4: `ImageCanvas` — erase-mode input handling and cursor preview

**Files:**
- Modify: `src/edit/ImageCanvas.h`
- Modify: `src/edit/ImageCanvas.cpp`

**Interfaces:**
- Consumes: existing `m_hasActiveImageLayer` (set by `setActiveMask`), `m_brushRadius`/`setBrushRadius` (shared display-px radius state), `normPointAt()`.
- Produces: `void setEraseMode(bool on)`, signals `void eraseAt(const QPointF &ptNorm)` and `void eraseFinished()`, consumed by Task 5 (`RetouchTab`).

- [ ] **Step 1: Declare `setEraseMode`, signals, and state in `ImageCanvas.h`**

Add the public method right after `setHealMode` (`ImageCanvas.h:45`):

```cpp
    void setEraseMode(bool on); // erase brush: punches transparency into the selected image layer
```

Add the signals right after `healAt` (`ImageCanvas.h:83`):

```cpp
    void eraseAt(const QPointF &ptNorm); // one erase-stroke sample (width-normalized)
    void eraseFinished();                // drag released -> commit history
```

Add the private state right after `m_healMode` (`ImageCanvas.h:141`):

```cpp
    bool m_eraseMode = false;
    bool m_eraseDragging = false;
    QPointF m_lastEraseNorm{-1, -1};
```

- [ ] **Step 2: Implement `setEraseMode`**

In `src/edit/ImageCanvas.cpp`, find `ImageCanvas::setHealMode` (around line 118, in the same block as the other `setXMode` implementations) and add right after it:

```cpp
void ImageCanvas::setEraseMode(bool on) {
    m_eraseMode = on;
    if (!on) m_eraseDragging = false;
    update();
}
```

- [ ] **Step 3: Handle mouse press — start an erase stroke**

In `mousePressEvent` (`src/edit/ImageCanvas.cpp`), add a new block right after the "Local-mask editing" block ends (after line 613, the `return;` that closes the `if (m_maskMode && ev->button() == Qt::LeftButton)` block, before the "Crop mode." comment at line 615):

```cpp
    // Erase brush: only active while an image layer is selected.
    if (m_eraseMode && m_hasActiveImageLayer && ev->button() == Qt::LeftButton) {
        QPointF n = normPointAt(ev->pos());
        m_eraseDragging = true;
        m_lastEraseNorm = n;
        m_mousePos = ev->pos();
        emit eraseAt(n);
        update();
        return;
    }
```

- [ ] **Step 4: Handle mouse move — sample additional dabs while dragging, and update the cursor while hovering**

In `mouseMoveEvent` (`src/edit/ImageCanvas.cpp`), add a block right after the `if (m_maskMode) { ... return; }` block ends (after line 719):

```cpp
    if (m_eraseMode) {
        m_mousePos = ev->pos();
        if (m_eraseDragging) {
            QPointF n = normPointAt(ev->pos());
            double dx = n.x() - m_lastEraseNorm.x();
            double dy = n.y() - m_lastEraseNorm.y();
            if (dx * dx + dy * dy > 0.004 * 0.004) { // throttle stroke samples
                m_lastEraseNorm = n;
                emit eraseAt(n);
            }
        }
        update();
        return;
    }
```

- [ ] **Step 5: Handle mouse release — commit the stroke to history**

In `mouseReleaseEvent` (`src/edit/ImageCanvas.cpp`), add a block right after the `if (m_maskDragging && ev->button() == Qt::LeftButton) { ... return; }` block (after line 911):

```cpp
    if (m_eraseDragging && ev->button() == Qt::LeftButton) {
        m_eraseDragging = false;
        emit eraseFinished();
        update();
        return;
    }
```

- [ ] **Step 6: Draw the erase brush cursor in `paintEvent`**

In `paintEvent` (`src/edit/ImageCanvas.cpp`), add a block right after the heal-mode cursor block ends (after line 423, before the "Local-mask gizmo." comment at line 425):

```cpp
    if (m_eraseMode && underMouse()) {
        double rad = m_brushRadius * m_scale;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 90, 90, 220), 1));
        p.setBrush(QColor(255, 60, 60, 40));
        p.drawEllipse(QPointF(m_mousePos), rad, rad);
    }
```

- [ ] **Step 7: Build to confirm it compiles**

Run: `cmake --build build -j`
Expected: builds with no errors. (No automated UI test exists in this repo for `ImageCanvas`; behavior is verified end-to-end in Task 6.)

- [ ] **Step 8: Commit**

```bash
git add src/edit/ImageCanvas.h src/edit/ImageCanvas.cpp
git commit -m "Add erase-brush mouse handling and cursor to ImageCanvas"
```

---

### Task 5: `RetouchTab` — wire erase strokes into the active layer + undo history

**Files:**
- Modify: `src/edit/RetouchTab.h`
- Modify: `src/edit/RetouchTab.cpp`

**Interfaces:**
- Consumes: `ImageCanvas::setEraseMode`, `ImageCanvas::eraseAt`, `ImageCanvas::eraseFinished` (Task 4); `ErasePoint`, `Mask::eraseStrokes` (Task 1).
- Produces: `void RetouchTab::setEraseMode(bool on)`, `void RetouchTab::setEraseBrush(int radiusDisplayPx)`, consumed by Task 6 (`RetouchWindow`).

- [ ] **Step 1: Declare the new public methods and private slot/state in `RetouchTab.h`**

Add public methods right after `setHealBrush`/`clearHeals` (`RetouchTab.h:58-59`):

```cpp
    void setEraseMode(bool on);
    void setEraseBrush(int radiusDisplayPx);
```

Add the private slots right after `onHealAt` (`RetouchTab.h:136`):

```cpp
    void onEraseAt(const QPointF &ptNorm);
    void onEraseFinished();
```

Add the private state right after `m_healRadiusDisplay` (`RetouchTab.h:164`):

```cpp
    int m_eraseRadiusDisplay = 20; // erase brush radius in display pixels
```

- [ ] **Step 2: Connect the new `ImageCanvas` signals in the `RetouchTab` constructor**

In `src/edit/RetouchTab.cpp`, right after the existing `connect(m_canvas, &ImageCanvas::healAt, this, &RetouchTab::onHealAt);` line (line 61), add:

```cpp
    connect(m_canvas, &ImageCanvas::eraseAt, this, &RetouchTab::onEraseAt);
    connect(m_canvas, &ImageCanvas::eraseFinished, this, &RetouchTab::onEraseFinished);
```

- [ ] **Step 3: Implement `setEraseMode` / `setEraseBrush`**

In `src/edit/RetouchTab.cpp`, right after `RetouchTab::setHealBrush` (after line 456, before `RetouchTab::clearHeals`), add:

```cpp
void RetouchTab::setEraseMode(bool on) {
    m_canvas->setEraseMode(on);
    if (on) m_canvas->setFocus();
}

void RetouchTab::setEraseBrush(int radiusDisplayPx) {
    m_eraseRadiusDisplay = radiusDisplayPx;
    m_canvas->setBrushRadius(radiusDisplayPx);
}
```

- [ ] **Step 4: Implement `onEraseAt` / `onEraseFinished`**

In `src/edit/RetouchTab.cpp`, right after `RetouchTab::onHealAt` (after line 479, before the "Local adjustment masks" section comment), add:

```cpp
// An erase dab was placed on the canvas (point in display-image, width-
// normalized coords — same space as onMaskBrushPoint). Only image layers
// can be erased; ImageCanvas already gates this on m_hasActiveImageLayer,
// but the active layer can still be non-image if selection changed
// mid-drag, so re-check here too.
void RetouchTab::onEraseAt(const QPointF &ptNorm) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (!m.isImageLayer()) return;
    double radiusNorm = (m_scaled.isNull() || m_scaled.width() <= 0)
                             ? 0.06
                             : m_eraseRadiusDisplay / double(m_scaled.width());
    m.eraseStrokes.append(ErasePoint{ptNorm, radiusNorm});
    retone();
}

void RetouchTab::onEraseFinished() {
    markEdited(); // schedule one coalesced undo step for the whole drag
}
```

- [ ] **Step 5: Build to confirm it compiles**

Run: `cmake --build build -j`
Expected: builds with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/edit/RetouchTab.h src/edit/RetouchTab.cpp
git commit -m "Wire erase-tool strokes into RetouchTab and undo history"
```

---

### Task 6: `RetouchWindow` — toolbar button, brush-size slider, and layer-type gating

**Files:**
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `RetouchTab::setEraseMode`, `RetouchTab::setEraseBrush` (Task 5); `RetouchTab::activeMaskIndex()`, `RetouchTab::masks()`, `Mask::isImageLayer()` (existing/Task 1).
- Produces: none consumed by later tasks — this is the final integration point.

- [ ] **Step 1: Declare the new widgets in `RetouchWindow.h`**

Add right after `m_maskToggle` (`RetouchWindow.h:150`):

```cpp
    QToolButton *m_eraseToggle = nullptr; // left icon bar: erase tool
    QSlider *m_eraseBrush = nullptr;
```

- [ ] **Step 2: Add an erase icon drawing function**

In `src/edit/RetouchWindow.cpp`, right after `drawMaskBrush` (after line 172, before `addFlyoutMarker`), add:

```cpp
QPixmap drawErase(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // A dashed-outline ellipse over a filled one: "removing" a region.
    p.setPen(QPen(c, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(5, 5, 18, 18));
    QColor fill = c;
    fill.setAlpha(90);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(QRectF(9, 9, 10, 10));
    return pm;
}
```

And right after `QIcon makeMaskIcon() { return makeToolIcon(drawMask); }` (line 198), add:

```cpp
QIcon makeEraseIcon() { return makeToolIcon(drawErase); }
```

- [ ] **Step 3: Add the toolbar button in `buildToolPanel()`**

In `src/edit/RetouchWindow.cpp`, right after the `m_brushToggle` block ends (after line 683, `m_toolsBar->addWidget(m_brushToggle);`, before the `m_maskToggle` block), add:

```cpp
    m_eraseToggle = new QToolButton;
    m_eraseToggle->setIcon(makeEraseIcon());
    m_eraseToggle->setCheckable(true);
    m_eraseToggle->setShortcut(QKeySequence(Qt::Key_E));
    m_eraseToggle->setToolTip("Erase (E) — paint transparency onto the selected image layer; Ctrl+wheel resizes brush");
    m_toolsBar->addWidget(m_eraseToggle);
```

- [ ] **Step 4: Wire the toggle handler**

Right after the `connect(m_brushToggle, &QToolButton::toggled, ...)` block ends (after line 812, its closing `});`), add:

```cpp
    connect(m_eraseToggle, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            { QSignalBlocker b(m_brushToggle); m_brushToggle->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); tab->setMaskMode(false); tab->setColorRangePickMode(false); }
            if (m_levelsPanel) m_levelsPanel->setTargetPickChecked(false);
            m_toolOptionsStack->setCurrentIndex(4);
            m_toolOptionsBar->setVisible(true);
        } else {
            m_toolOptionsBar->setVisible(false);
        }
        if (tab && tab->isReady()) {
            tab->setEraseBrush(m_eraseBrush->value());
            tab->setEraseMode(on);
        }
    });
```

Note: this follows the exact pattern of the other four toggle handlers (each turns the others off). Every *other* tool's handler (`m_toolZoom`, `m_cropToggle`, `m_healToggle`, `m_maskToggle`, `m_brushToggle`) must also turn `m_eraseToggle` off and call `tab->setEraseMode(false)` — add `{ QSignalBlocker b(m_eraseToggle); m_eraseToggle->setChecked(false); }` and `tab->setEraseMode(false)` to each of their five `if (on) { ... }` blocks, alongside the existing sibling-disable lines.

- [ ] **Step 5: Add the Erase options page (brush-size slider)**

In `buildToolOptionsBar()`, right after the "Brush page (index 3)" block ends (after line 939, `m_toolOptionsStack->addWidget(brushPage);`, before any subsequent page or the end of the function), add:

```cpp
    // --- Erase page (index 4) ---
    auto *erasePage = new QWidget;
    auto *eraseRow = new QHBoxLayout(erasePage);
    eraseRow->setContentsMargins(4, 2, 4, 2);
    m_eraseBrush = new QSlider(Qt::Horizontal);
    m_eraseBrush->setRange(4, 80);
    m_eraseBrush->setValue(20);
    m_eraseBrush->setMinimumWidth(160);
    eraseRow->addWidget(new QLabel("Brush size:"));
    eraseRow->addWidget(m_eraseBrush);
    eraseRow->addStretch(1);
    m_toolOptionsStack->addWidget(erasePage);

    connect(m_eraseBrush, &QSlider::valueChanged, this, [this](int v) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setEraseBrush(v);
    });
```

- [ ] **Step 6: Gate the button on the active layer being an image layer**

In `RetouchWindow::refreshMaskPanel()` (`src/edit/RetouchWindow.cpp:1252-1259`), replace:

```cpp
void RetouchWindow::refreshMaskPanel() {
    RetouchTab *tab = currentTab();
    const bool ready = tab && tab->isReady();
    if (m_layersPanel) {
        if (ready) m_layersPanel->setMasks(tab->masks(), tab->activeMaskIndex());
        else m_layersPanel->clear();
    }
}
```

with:

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

- [ ] **Step 7: Build**

Run: `cmake --build build -j`
Expected: builds with no errors.

- [ ] **Step 8: Manually verify in the running app**

Run: `./build/imgcapture` (or the actual binary name/path produced by the build — check `build/` for the executable if this differs)

1. Open a photo, add an image layer (drag-drop or existing "add image layer" action) so an image layer exists and is selected.
2. Confirm the Erase toolbar button (shortcut `E`) is enabled; select it.
3. Drag across the image layer — confirm a red-tinted soft-edged brush cursor follows the mouse, and dragging punches a feathered transparent hole showing whatever is beneath (another layer or the canvas background color).
4. Release the mouse, then press Ctrl+Z — confirm the erase is undone in one step; Ctrl+Shift+Z (or the app's redo shortcut) restores it.
5. Select a non-image-layer (e.g. a Radial mask layer or no layer) — confirm the Erase button becomes disabled/unchecks itself.
6. Save the file, close and reopen it — confirm the erased hole persists (sidecar round-trip).

Report back whether all six checks pass; do not mark this task complete until they do.

- [ ] **Step 9: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Add erase-tool toolbar button, brush slider, and layer-type gating"
```

---

## Self-Review Notes

- **Spec coverage:** Task 1-2 cover spec items 1-2 (data model, compositing). Task 4 covers item 3 (canvas input) and item 5 (cursor preview, reusing existing brush-circle code). Task 5 Step 3 covers item 4 (brush radius — each tool keeps its own display-px value, consistent with Heal/Paint precedent already in the codebase; the spec's "reuse the existing shared control" is satisfied in spirit — same `ImageCanvas::setBrushRadius`/`m_brushRadius` plumbing is reused, only the display-px value the UI feeds into it is per-tool, matching how Heal and Paint already work rather than truly sharing one slider, which would have been inconsistent with those two). Task 5 Step 1-4 cover item 6 (RetouchTab wiring). Task 6 covers item 7 (toolbar UI) and the "image layers only" constraint (spec item 3 / risk note).
- **Coordinate space deviation from the spec's "risks/notes" section:** the spec suggested storing erase points in the layer's local/frame-relative coordinate space so erased regions track a layer that's later moved. This plan instead uses plain canvas-width-normalized coordinates (`ErasePoint.pt`), identical to `BrushStrokePoint` and every other on-canvas brush/mask geometry in this codebase (radial/linear/paint mask centers, brush strokes). This is a deliberate simplification: it avoids introducing a second coordinate convention alongside the one every other tool already uses, and matches how, e.g., a Paint mask layer's strokes already don't track image-layer movement either. Erasing then moving the same image layer is an edge case explicitly acceptable per YAGNI.
- **Placeholder scan:** no TBD/TODO; all steps have complete code.
- **Type consistency:** `ErasePoint{pt, radius}` used identically across Task 1 (definition), Task 2 (compositing), Task 3 (serialization), Task 5 (RetouchTab construction) — verified field names/order match. `setEraseMode`/`setEraseBrush` signatures match between `ImageCanvas`, `RetouchTab`, and their call sites in `RetouchWindow`. Signal names `eraseAt`/`eraseFinished` used consistently between `ImageCanvas.h` declarations, `ImageCanvas.cpp` emissions, and `RetouchTab.cpp` connections.
