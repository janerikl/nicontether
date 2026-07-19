# Tap-to-Focus Coordinate Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make click-to-focus in the live view focus the correct spot on Nikon DSLRs by mapping the click into the camera's AF coordinate frame (a persisted, adjustable size), and draw a colored AF reticle at the clicked point.

**Architecture:** The click→AF-coordinate math moves into a pure, Qt-free header function so it can be unit-tested standalone. `LiveViewWidget` uses it with an adjustable AF-frame size and renders a reticle whose color reflects the AF command result. The camera worker gains an `afAreaResult(bool)` signal that flows worker → controller → `TetherView` → widget. `ControlsPanel` gains two spin boxes (AF frame width/height) persisted via QSettings.

**Tech Stack:** C++17, Qt6 (Widgets), libgphoto2, CMake. Existing QSettings scope is org/app "NikonTether".

## Global Constraints

- C++ standard: C++17 (`CMAKE_CXX_STANDARD 17`).
- Qt: Qt6::Widgets, `CMAKE_AUTOMOC ON`. New QObject-derived headers are auto-moc'd; new source files must be added to the `nikontether` target in `CMakeLists.txt`.
- QSettings uses the default constructor (no args) — the app already relies on the global org/app scope "NikonTether". New keys: `af/frameWidth`, `af/frameHeight` (ints).
- AF coordinate convention (Nikon `changeafarea` / PTP `0x9205`): x,y are the **center of the AF box**, in the coordinate space of the live-view header's `ImageWidth × ImageHeight`. That header is discarded by libgphoto2, so the frame size is a user-adjustable setting, default `640 × 426` (a starting point for the D750 / D7x00 / D5x00 family — calibrated by the user).
- Follow existing patterns: worker signals are re-emitted by `CameraController` with queued connections; `TetherView::buildUi()` wires controller↔UI.

---

### Task 1: Pure click→AF-coordinate mapping (Qt-free) + standalone test

Extract the coordinate math into a header-only, dependency-free function and cover it with a tiny assert-based test executable registered with CTest. This is the correctness core of the fix.

**Files:**
- Create: `src/ui/AfMapping.h`
- Create: `tests/AfMappingTest.cpp`
- Modify: `CMakeLists.txt` (add `enable_testing()` + test target, after the install blocks, at end of file)

**Interfaces:**
- Consumes: nothing.
- Produces: `afmap::Result afmap::mapClickToAf(int px, int py, int rx, int ry, int rw, int rh, int fw, int fh)` where `struct Result { int x; int y; bool valid; };`. Returns `valid=false` when the click is outside the drawn rect or any dimension is non-positive; otherwise `x,y` are the AF-frame coordinates (rounded, clamped to `[0,fw]`/`[0,fh]`).

- [ ] **Step 1: Write the failing test**

Create `tests/AfMappingTest.cpp`:

```cpp
#include "ui/AfMapping.h"

#include <cassert>
#include <cstdio>

int main() {
    using afmap::mapClickToAf;

    // Drawn image rect at (0,0) size 640x480; AF frame 640x480 (1:1).
    // Center click -> center of AF frame.
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 640, 480);
        assert(r.valid);
        assert(r.x == 320 && r.y == 240);
    }

    // Different AF frame scale (320x240): center still maps to center.
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 320, 240);
        assert(r.valid);
        assert(r.x == 160 && r.y == 120);
    }

    // Top-left corner of drawn rect -> (0,0).
    {
        auto r = mapClickToAf(0, 0, 0, 0, 640, 480, 320, 240);
        assert(r.valid && r.x == 0 && r.y == 0);
    }

    // Letterboxed rect offset by (100,50): click at rect origin -> (0,0).
    {
        auto r = mapClickToAf(100, 50, 100, 50, 640, 480, 320, 240);
        assert(r.valid && r.x == 0 && r.y == 0);
    }

    // Click above/left of the drawn rect -> invalid.
    {
        auto r = mapClickToAf(99, 49, 100, 50, 640, 480, 320, 240);
        assert(!r.valid);
    }

    // Non-positive AF frame -> invalid (caller falls back to frame size).
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 0, 0);
        assert(!r.valid);
    }

    std::puts("AfMappingTest: all assertions passed");
    return 0;
}
```

- [ ] **Step 2: Add the CMake test target**

Append to the end of `CMakeLists.txt`:

```cmake
enable_testing()
add_executable(af_mapping_test tests/AfMappingTest.cpp)
target_include_directories(af_mapping_test PRIVATE src)
add_test(NAME af_mapping_test COMMAND af_mapping_test)
```

- [ ] **Step 3: Run the test to verify it fails to build**

Run:
```bash
cmake -S . -B build && cmake --build build --target af_mapping_test
```
Expected: FAIL — compile error, `ui/AfMapping.h` not found (file does not exist yet).

- [ ] **Step 4: Write the minimal implementation**

Create `src/ui/AfMapping.h`:

```cpp
#pragma once

// Pure, Qt-free mapping from a click in the drawn (letterboxed) live-view
// image to the camera's AF coordinate frame. Kept dependency-free so it can be
// unit-tested without a QApplication.
//
// The displayed image and the AF frame both cover the full live-view field of
// view, so a normalized position within the drawn rect maps directly to the AF
// frame. fw/fh is the AF coordinate frame size (Nikon header ImageWidth/Height),
// which libgphoto2 does not expose, hence it is a user-adjustable setting.
namespace afmap {

struct Result {
    int x = 0;
    int y = 0;
    bool valid = false;
};

// px,py: click in widget coordinates.
// rx,ry,rw,rh: the rect where the image is painted (letterboxed).
// fw,fh: AF coordinate frame size.
inline Result mapClickToAf(int px, int py, int rx, int ry, int rw, int rh,
                           int fw, int fh) {
    if (rw <= 0 || rh <= 0 || fw <= 0 || fh <= 0) return {};
    if (px < rx || py < ry || px >= rx + rw || py >= ry + rh) return {};

    double nx = double(px - rx) / rw;
    double ny = double(py - ry) / rh;
    int ax = int(nx * fw + 0.5);
    int ay = int(ny * fh + 0.5);
    if (ax < 0) ax = 0;
    if (ax > fw) ax = fw;
    if (ay < 0) ay = 0;
    if (ay > fh) ay = fh;
    return {ax, ay, true};
}

} // namespace afmap
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cmake --build build --target af_mapping_test && ctest --test-dir build -R af_mapping_test --output-on-failure
```
Expected: PASS — `AfMappingTest: all assertions passed`, `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/AfMapping.h tests/AfMappingTest.cpp CMakeLists.txt
git commit -m "feat: add pure click-to-AF-coordinate mapping with test"
```

---

### Task 2: LiveViewWidget uses AF frame + draws reticle

Replace the JPEG-pixel scaling with the AF-frame mapping from Task 1, add an adjustable AF frame size, and render a colored reticle at the clicked point.

**Files:**
- Modify: `src/ui/LiveViewWidget.h`
- Modify: `src/ui/LiveViewWidget.cpp`

**Interfaces:**
- Consumes: `afmap::mapClickToAf(...)` from Task 1.
- Produces (public API used by Tasks 3 & 4):
  - `void LiveViewWidget::setAfFrameSize(int w, int h);`
  - `void LiveViewWidget::setAfResult(bool ok);`
  - `void LiveViewWidget::clearReticle();`
  - existing signal `void focusRequested(int sensorX, int sensorY);` is unchanged in signature (now carries AF-frame coordinates).

- [ ] **Step 1: Update the header**

In `src/ui/LiveViewWidget.h`, replace the class body (lines 8-27) with:

```cpp
class LiveViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit LiveViewWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void clearFrame();

    // AF coordinate frame size (Nikon header ImageWidth/Height). When either is
    // <= 0, clicks fall back to the decoded frame's own pixel dimensions.
    void setAfFrameSize(int w, int h);

public slots:
    // Update the reticle color after an AF-area command: green on success,
    // red on failure.
    void setAfResult(bool ok);
    // Remove the reticle (e.g. when live view stops).
    void clearReticle();

signals:
    void focusRequested(int sensorX, int sensorY);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect drawnRect() const; // where the frame is painted (letterboxed)

    QImage m_frame;
    int m_afFrameW = 0; // <= 0 => fall back to frame width
    int m_afFrameH = 0; // <= 0 => fall back to frame height

    enum class AfState { Pending, Ok, Failed };
    bool m_hasReticle = false;
    QPointF m_reticleNorm;               // 0..1 position within the drawn image
    AfState m_afState = AfState::Pending;
    double m_afBoxFrac = 0.12;           // reticle box size as fraction of min(drawn w,h)
};
```

- [ ] **Step 2: Add the include and setters in the .cpp**

In `src/ui/LiveViewWidget.cpp`, add the mapping include after line 4 (`#include <QMouseEvent>`):

```cpp
#include "ui/AfMapping.h"
```

Then add these methods immediately after `clearFrame()` (after line 22):

```cpp
void LiveViewWidget::setAfFrameSize(int w, int h) {
    m_afFrameW = w;
    m_afFrameH = h;
}

void LiveViewWidget::setAfResult(bool ok) {
    if (!m_hasReticle) return;
    m_afState = ok ? AfState::Ok : AfState::Failed;
    update();
}

void LiveViewWidget::clearReticle() {
    m_hasReticle = false;
    update();
}
```

- [ ] **Step 3: Rewrite mousePressEvent to use the mapping and arm the reticle**

In `src/ui/LiveViewWidget.cpp`, replace the whole `mousePressEvent` (lines 44-54) with:

```cpp
void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;

    // AF frame size, falling back to the decoded frame's own pixels if unset.
    int fw = m_afFrameW > 0 ? m_afFrameW : m_frame.width();
    int fh = m_afFrameH > 0 ? m_afFrameH : m_frame.height();

    afmap::Result af = afmap::mapClickToAf(ev->pos().x(), ev->pos().y(),
                                           r.x(), r.y(), r.width(), r.height(),
                                           fw, fh);
    if (!af.valid) return;

    // Arm a pending reticle at the normalized click position.
    m_reticleNorm = QPointF(double(ev->pos().x() - r.x()) / r.width(),
                            double(ev->pos().y() - r.y()) / r.height());
    m_afState = AfState::Pending;
    m_hasReticle = true;
    update();

    emit focusRequested(af.x, af.y);
}
```

- [ ] **Step 4: Draw the reticle in paintEvent**

In `src/ui/LiveViewWidget.cpp`, replace the final line of `paintEvent` (`painter.drawImage(drawnRect(), m_frame);`, line 41) with:

```cpp
    QRect r = drawnRect();
    painter.drawImage(r, m_frame);

    if (m_hasReticle) {
        int side = int(m_afBoxFrac * qMin(r.width(), r.height()));
        QPointF c(r.x() + m_reticleNorm.x() * r.width(),
                  r.y() + m_reticleNorm.y() * r.height());
        QRectF box(c.x() - side / 2.0, c.y() - side / 2.0, side, side);
        QColor color = m_afState == AfState::Ok       ? QColor(0, 200, 0)
                       : m_afState == AfState::Failed  ? QColor(220, 0, 0)
                                                       : QColor(230, 200, 0);
        QPen pen(color, 2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);
    }
```

Add the required include after line 3 (`#include <QPainter>`):

```cpp
#include <QtMath>
```

- [ ] **Step 5: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS — links successfully (warnings acceptable, no errors).

- [ ] **Step 6: Commit**

```bash
git add src/ui/LiveViewWidget.h src/ui/LiveViewWidget.cpp
git commit -m "feat: map live-view click to AF frame and draw focus reticle"
```

---

### Task 3: AF-area result signal (worker → controller → view → reticle)

Report whether the `changeafarea` command was accepted so the reticle can turn green or red. Wire it end-to-end.

**Files:**
- Modify: `src/camera/CameraWorker.h` (add signal declaration)
- Modify: `src/camera/CameraWorker.cpp` (emit from `setAfArea`, lines 213-227)
- Modify: `src/camera/CameraController.h` (add signal)
- Modify: `src/camera/CameraController.cpp` (re-emit, connect block lines 14-21)
- Modify: `src/ui/TetherView.cpp` (connect controller → live view)

**Interfaces:**
- Consumes: `LiveViewWidget::setAfResult(bool)` from Task 2.
- Produces: `void CameraWorker::afAreaResult(bool ok);` and `void CameraController::afAreaResult(bool ok);` (signals).

- [ ] **Step 1: Declare the worker signal**

In `src/camera/CameraWorker.h`, add to the `signals:` block (after line 43, `void log(...)`):

```cpp
    void afAreaResult(bool ok);
```

- [ ] **Step 2: Emit the result from setAfArea**

In `src/camera/CameraWorker.cpp`, replace the whole `setAfArea` body (lines 213-227) with:

```cpp
void CameraWorker::setAfArea(int x, int y) {
    void *w = nullptr, *r = nullptr;
    if (!findWidget("changeafarea", &w, &r)) {
        emit log("Focus-point selection not available on this camera.");
        emit afAreaResult(false);
        return;
    }
    CameraWidget *child = static_cast<CameraWidget *>(w);
    CameraWidget *root = static_cast<CameraWidget *>(r);
    QString coord = QString("%1x%2").arg(x).arg(y);
    gp_widget_set_value(child, coord.toUtf8().constData());
    int ret = gp_camera_set_config(m_cam, root, m_ctx);
    gp_widget_free(root);
    if (ret != GP_OK) {
        reportError("set AF area", ret);
        emit afAreaResult(false);
        return;
    }
    triggerAutofocus();
    emit afAreaResult(true);
}
```

- [ ] **Step 3: Declare and re-emit the controller signal**

In `src/camera/CameraController.h`, add to the `signals:` block (after line 38, `void log(...)`):

```cpp
    void afAreaResult(bool ok);
```

In `src/camera/CameraController.cpp`, add to the re-emit connect block (after line 21, the `log` connect):

```cpp
    connect(m_worker, &CameraWorker::afAreaResult, this, &CameraController::afAreaResult);
```

- [ ] **Step 4: Connect controller → live view in TetherView**

In `src/ui/TetherView.cpp`, immediately after the existing `focusRequested` connection (lines 45-46), add:

```cpp
    connect(m_controller, &CameraController::afAreaResult,
            m_liveView, &LiveViewWidget::setAfResult);
```

- [ ] **Step 5: Build to verify it compiles and links**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS — no errors; the new queued signal/slot resolves (both args are `bool`, a registered metatype).

- [ ] **Step 6: Commit**

```bash
git add src/camera/CameraWorker.h src/camera/CameraWorker.cpp \
        src/camera/CameraController.h src/camera/CameraController.cpp \
        src/ui/TetherView.cpp
git commit -m "feat: report AF-area command result to color the focus reticle"
```

---

### Task 4: Calibration spin boxes in ControlsPanel + QSettings persistence

Add two spin boxes (AF frame width/height) so the user can calibrate, persist them via QSettings, and seed the live-view widget on startup and on change.

**Files:**
- Modify: `src/ui/ControlsPanel.h`
- Modify: `src/ui/ControlsPanel.cpp`
- Modify: `src/ui/TetherView.cpp`

**Interfaces:**
- Consumes: `LiveViewWidget::setAfFrameSize(int,int)` from Task 2.
- Produces: `void ControlsPanel::afFrameSizeChanged(int w, int h);` (signal, emitted on load and on edit).

- [ ] **Step 1: Update the ControlsPanel header**

In `src/ui/ControlsPanel.h`:

Add the forward declaration after line 9 (`class QLabel;`):

```cpp
class QSpinBox;
```

Add to the `signals:` block (after line 28, `void captureRequested();`):

```cpp
    // AF coordinate frame size for click-to-focus calibration.
    void afFrameSizeChanged(int w, int h);
```

Add to the `private:` members (after line 39, `m_captureButton`):

```cpp
    QSpinBox *m_afFrameW = nullptr;
    QSpinBox *m_afFrameH = nullptr;
    void loadAfFrameSettings();
```

- [ ] **Step 2: Build the spin boxes and wire persistence**

In `src/ui/ControlsPanel.cpp`, add includes after line 8 (`#include <QSignalBlocker>`):

```cpp
#include <QSpinBox>
#include <QSettings>
```

In the constructor, insert this block immediately after `outer->addStretch(1);` (line 19) and before the `m_afButton`/`m_captureButton` construction, so the calibration fields sit above the AF/Capture buttons:

```cpp
    // Click-to-focus calibration: AF coordinate frame size. Nikon's changeafarea
    // wants coordinates in the live-view header's ImageWidth/Height frame, which
    // libgphoto2 discards — so it is user-adjustable. Center is always correct;
    // tune these until edge clicks focus where the reticle is drawn.
    auto *afForm = new QFormLayout;
    m_afFrameW = new QSpinBox;
    m_afFrameH = new QSpinBox;
    m_afFrameW->setRange(1, 20000);
    m_afFrameH->setRange(1, 20000);
    afForm->addRow("AF frame width:", m_afFrameW);
    afForm->addRow("AF frame height:", m_afFrameH);
    outer->addLayout(afForm);

    loadAfFrameSettings();

    auto persist = [this]() {
        QSettings s;
        s.setValue("af/frameWidth", m_afFrameW->value());
        s.setValue("af/frameHeight", m_afFrameH->value());
        emit afFrameSizeChanged(m_afFrameW->value(), m_afFrameH->value());
    };
    connect(m_afFrameW, qOverload<int>(&QSpinBox::valueChanged), this,
            [persist](int) { persist(); });
    connect(m_afFrameH, qOverload<int>(&QSpinBox::valueChanged), this,
            [persist](int) { persist(); });
```

Note: `outer` is the `QVBoxLayout*` created at the top of the constructor. This block only references `outer`, `m_afFrameW`, and `m_afFrameH`, so placing it right after `outer->addStretch(1);` is safe.

Then add the loader method at the end of the file:

```cpp
void ControlsPanel::loadAfFrameSettings() {
    QSettings s;
    // Default 640x426: a starting point for the D750 / D7x00 / D5x00 family.
    // The user calibrates from here.
    int w = s.value("af/frameWidth", 640).toInt();
    int h = s.value("af/frameHeight", 426).toInt();
    QSignalBlocker bw(m_afFrameW);
    QSignalBlocker bh(m_afFrameH);
    m_afFrameW->setValue(w);
    m_afFrameH->setValue(h);
}
```

- [ ] **Step 3: Emit the loaded value once at construction end**

In `src/ui/ControlsPanel.cpp`, at the very end of the constructor (after `setEnabledControls(false);`, line 34), add:

```cpp
    emit afFrameSizeChanged(m_afFrameW->value(), m_afFrameH->value());
```

(This emission has no receivers yet — TetherView connects afterward — so it is harmless. The live view is seeded explicitly in Task 4 Step 4; this line just keeps ControlsPanel self-consistent if a receiver is ever connected before construction completes.)

- [ ] **Step 4: Wire ControlsPanel → LiveViewWidget in TetherView**

In `src/ui/TetherView.cpp`, in the UI wiring section near the other `m_controls` connects (after line 44, the `captureRequested` connect), add:

```cpp
    connect(m_controls, &ControlsPanel::afFrameSizeChanged,
            m_liveView, &LiveViewWidget::setAfFrameSize);
```

Then, still in `buildUi()` after both `m_controls` and `m_liveView` exist and are connected, seed the initial value explicitly so it applies regardless of construction order:

```cpp
    // Seed the live view with the persisted AF frame size.
    {
        QSettings s;
        m_liveView->setAfFrameSize(s.value("af/frameWidth", 640).toInt(),
                                   s.value("af/frameHeight", 426).toInt());
    }
```

Add the include at the top of `src/ui/TetherView.cpp` if not present (check the existing includes first):

```cpp
#include <QSettings>
```

- [ ] **Step 5: Build to verify it compiles and links**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS — no errors. The spin boxes appear in the Camera Controls dock.

- [ ] **Step 6: Manual verification (with camera) + commit**

Manual check (documented, requires the D750-family body in live view):
1. Start live view, click the center — camera focuses center, yellow reticle appears, turns green.
2. Click a corner — if AF lands too far inward/outward, adjust "AF frame width/height" until AF matches the reticle. Confirm the values persist across a restart.
3. Click a non-Nikon-supported case (or disconnect): reticle turns red.

Commit:
```bash
git add src/ui/ControlsPanel.h src/ui/ControlsPanel.cpp src/ui/TetherView.cpp
git commit -m "feat: add AF-frame calibration spin boxes with persistence"
```

---

## Notes for the implementer

- `bool` is already a registered Qt metatype, so the new queued `afAreaResult(bool)` signal needs no `qRegisterMetaType`.
- The `focusRequested` signal keeps its name and `(int,int)` signature; only the meaning of the values changes (AF-frame coords instead of JPEG pixels). No connection sites change except the additions above.
- If `tests/` does not exist, `git add` of `tests/AfMappingTest.cpp` creates it; no separate mkdir step is needed.
- Build directory is `build/` (created by `cmake -S . -B build`). If the repo already has a `build/`, reuse it.
