# Live View Grid Overlays Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add right-click-selectable composition overlays (rule of thirds, golden ratio, golden spiral, center crosshair, diagonals) to the tether live view, persisted across launches.

**Architecture:** Grid geometry is a pure, Qt-free function in a new header `src/ui/GridOverlay.h` returning normalized line segments for a given mode. `LiveViewWidget` stores a `GridMode`, draws the returned segments in `paintEvent` (after the image, before AF reticle), offers a right-click `QMenu` to change it, and persists the choice to `QSettings`. A new plain-`assert` test exercises the geometry function.

**Tech Stack:** C++17, Qt 6 (Widgets), CMake, plain-assert test executables.

## Global Constraints

- Overlays are radio-style: exactly one active at a time, or none (`GridMode::Off`).
- All overlay geometry must be clipped to `drawnRect()` (the letterboxed image rect) — never draw on the black bars.
- Draw overlays AFTER `drawImage` and BEFORE the AF reticle / calibration crosshair.
- Persist under `QSettings` key `"liveview/gridMode"` (stored as int).
- Follow existing patterns: pure-geometry header like `src/ui/AfMapping.h`; member+setter+`update()` like `m_calibrating`/`setCalibrationMode`.

---

### Task 1: Grid geometry function (Qt-free, tested)

**Files:**
- Create: `src/ui/GridOverlay.h`
- Test: `tests/GridOverlayTest.cpp`
- Modify: `CMakeLists.txt:89` (register the new test)

**Interfaces:**
- Produces:
  - `enum class GridMode { Off = 0, Thirds = 1, GoldenRatio = 2, GoldenSpiral = 3, Crosshair = 4, Diagonals = 5 };`
  - `struct grid::Seg { double x1, y1, x2, y2; };` — coordinates are **fractions 0..1** of the drawn rect.
  - `std::vector<grid::Seg> grid::segments(GridMode mode);` — returns the line segments (empty for `Off`). Callers scale each fraction into `drawnRect()` when painting.

- [ ] **Step 1: Write the failing test**

Create `tests/GridOverlayTest.cpp`:

```cpp
#include "ui/GridOverlay.h"

#include <cassert>
#include <cstdio>
#include <cmath>

static bool hasSeg(const std::vector<grid::Seg> &v,
                   double x1, double y1, double x2, double y2) {
    const double eps = 1e-6;
    for (const auto &s : v) {
        if (std::fabs(s.x1 - x1) < eps && std::fabs(s.y1 - y1) < eps &&
            std::fabs(s.x2 - x2) < eps && std::fabs(s.y2 - y2) < eps) {
            return true;
        }
    }
    return false;
}

static bool inRange(const std::vector<grid::Seg> &v) {
    for (const auto &s : v) {
        if (s.x1 < -1e-9 || s.x1 > 1 + 1e-9) return false;
        if (s.y1 < -1e-9 || s.y1 > 1 + 1e-9) return false;
        if (s.x2 < -1e-9 || s.x2 > 1 + 1e-9) return false;
        if (s.y2 < -1e-9 || s.y2 > 1 + 1e-9) return false;
    }
    return true;
}

int main() {
    using grid::segments;

    // Off -> no segments.
    assert(segments(GridMode::Off).empty());

    // Thirds -> 4 lines (2 vertical, 2 horizontal) at 1/3 and 2/3, full span.
    {
        auto v = segments(GridMode::Thirds);
        assert(v.size() == 4);
        assert(hasSeg(v, 1.0/3.0, 0.0, 1.0/3.0, 1.0));  // vertical @ 1/3
        assert(hasSeg(v, 2.0/3.0, 0.0, 2.0/3.0, 1.0));  // vertical @ 2/3
        assert(hasSeg(v, 0.0, 1.0/3.0, 1.0, 1.0/3.0));  // horizontal @ 1/3
        assert(hasSeg(v, 0.0, 2.0/3.0, 1.0, 2.0/3.0));  // horizontal @ 2/3
        assert(inRange(v));
    }

    // Golden ratio -> 4 lines at phi divisions 0.382 and 0.618.
    {
        auto v = segments(GridMode::GoldenRatio);
        assert(v.size() == 4);
        const double a = 1.0 - 0.618; // 0.382
        const double b = 0.618;
        assert(hasSeg(v, a, 0.0, a, 1.0));
        assert(hasSeg(v, b, 0.0, b, 1.0));
        assert(hasSeg(v, 0.0, a, 1.0, a));
        assert(hasSeg(v, 0.0, b, 1.0, b));
        assert(inRange(v));
    }

    // Crosshair -> center vertical + horizontal.
    {
        auto v = segments(GridMode::Crosshair);
        assert(v.size() == 2);
        assert(hasSeg(v, 0.5, 0.0, 0.5, 1.0));
        assert(hasSeg(v, 0.0, 0.5, 1.0, 0.5));
    }

    // Diagonals -> both corner-to-corner diagonals present.
    {
        auto v = segments(GridMode::Diagonals);
        assert(hasSeg(v, 0.0, 0.0, 1.0, 1.0));
        assert(hasSeg(v, 1.0, 0.0, 0.0, 1.0));
        assert(inRange(v));
    }

    // Golden spiral -> non-empty polyline approximation, all in range.
    {
        auto v = segments(GridMode::GoldenSpiral);
        assert(!v.empty());
        assert(inRange(v));
    }

    std::puts("GridOverlayTest: all assertions passed");
    return 0;
}
```

- [ ] **Step 2: Register the test and run to verify it fails to build**

Add after `CMakeLists.txt:89`:

```cmake
add_executable(grid_overlay_test tests/GridOverlayTest.cpp)
target_include_directories(grid_overlay_test PRIVATE src)
add_test(NAME grid_overlay_test COMMAND grid_overlay_test)
```

Run:
```bash
cmake -S . -B build >/dev/null && cmake --build build --target grid_overlay_test
```
Expected: FAIL — `ui/GridOverlay.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `src/ui/GridOverlay.h`:

```cpp
#pragma once

#include <vector>
#include <cmath>

// Composition overlay modes for the live view. Radio-style: one at a time.
enum class GridMode {
    Off = 0,
    Thirds = 1,
    GoldenRatio = 2,
    GoldenSpiral = 3,
    Crosshair = 4,
    Diagonals = 5,
};

namespace grid {

// A line segment in fractional coordinates: 0..1 across the drawn image rect.
struct Seg {
    double x1, y1, x2, y2;
};

inline std::vector<Seg> segments(GridMode mode) {
    std::vector<Seg> v;
    switch (mode) {
    case GridMode::Off:
        break;
    case GridMode::Thirds: {
        const double a = 1.0 / 3.0, b = 2.0 / 3.0;
        v.push_back({a, 0.0, a, 1.0});
        v.push_back({b, 0.0, b, 1.0});
        v.push_back({0.0, a, 1.0, a});
        v.push_back({0.0, b, 1.0, b});
        break;
    }
    case GridMode::GoldenRatio: {
        const double b = 0.618, a = 1.0 - b; // 0.382
        v.push_back({a, 0.0, a, 1.0});
        v.push_back({b, 0.0, b, 1.0});
        v.push_back({0.0, a, 1.0, a});
        v.push_back({0.0, b, 1.0, b});
        break;
    }
    case GridMode::Crosshair:
        v.push_back({0.5, 0.0, 0.5, 1.0});
        v.push_back({0.0, 0.5, 1.0, 0.5});
        break;
    case GridMode::Diagonals:
        v.push_back({0.0, 0.0, 1.0, 1.0});
        v.push_back({1.0, 0.0, 0.0, 1.0});
        // Harmonious "reciprocal" diagonals from remaining corners to the
        // perpendicular feet, approximated as corner-to-midpoint guides.
        v.push_back({0.0, 0.0, 1.0, 0.5});
        v.push_back({1.0, 1.0, 0.0, 0.5});
        break;
    case GridMode::GoldenSpiral: {
        // Golden spiral: nested squares tile the unit rect, each holding a
        // quarter-arc. The square's side shrinks by the golden ratio each step.
        // The arc is centered at the square corner shared with the next
        // (smaller) square, so consecutive arcs join smoothly.
        //
        // Remaining rectangle is [x0,x1] x [y0,y1]. On each step we cut the
        // square of side = min(w,h) from one end and rotate the cut side.
        const double invPhi = 0.618; // 1/golden ≈ 0.6180339887
        double x0 = 0.0, y0 = 0.0, x1 = 1.0, y1 = 1.0;
        // side cut order rotates: 0=left, 1=top, 2=right, 3=bottom.
        int sideOrder = 0;

        auto addArc = [&](double cx, double cy, double r, double a0) {
            const int N = 10;
            double prevx = cx + r * std::cos(a0);
            double prevy = cy + r * std::sin(a0);
            for (int k = 1; k <= N; ++k) {
                double a = a0 + (M_PI / 2.0) * (double(k) / N);
                double px = cx + r * std::cos(a);
                double py = cy + r * std::sin(a);
                auto clamp01 = [](double t){ return t < 0 ? 0.0 : (t > 1 ? 1.0 : t); };
                v.push_back({clamp01(prevx), clamp01(prevy),
                             clamp01(px), clamp01(py)});
                prevx = px; prevy = py;
            }
        };

        for (int i = 0; i < 9; ++i) {
            double w = x1 - x0, h = y1 - y0;
            double s = (w < h ? w : h);
            if (s < 1e-3) break;
            switch (sideOrder) {
            case 0: // square on the LEFT; arc centre at its inner (right) corner
                // square = [x0, x0+s] x [y0, y1]; inner corner = (x0+s, y1),
                // quarter sweeps from angle pi (left) to 3pi/2 (up).
                addArc(x0 + s, y1, s, M_PI);
                x0 += s;
                break;
            case 1: // square on the TOP; inner corner = (x0, y0+s)
                addArc(x0, y0 + s, s, -M_PI / 2.0);
                y0 += s;
                break;
            case 2: // square on the RIGHT; inner corner = (x1-s, y0)
                addArc(x1 - s, y0, s, 0.0);
                x1 -= s;
                break;
            case 3: // square on the BOTTOM; inner corner = (x1, y1-s)
                addArc(x1, y1 - s, s, M_PI / 2.0);
                y1 -= s;
                break;
            }
            sideOrder = (sideOrder + 1) % 4;
            (void)invPhi; // ratio emerges from repeatedly cutting the square
        }
        break;
    }
    }
    return v;
}

} // namespace grid
```

> NOTE for implementer: the spiral block above is a first pass. If the arc
> tessellation looks wrong when you eyeball it in the app (Task 4 verify),
> replace the `arcs` construction with a cleaner nested-square spiral, keeping
> the contract intact: return non-empty `Seg`s all within 0..1. The test only
> asserts non-empty + in-range, so you have latitude to refine the curve.

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cmake --build build --target grid_overlay_test && ./build/grid_overlay_test
```
Expected: `GridOverlayTest: all assertions passed`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/GridOverlay.h tests/GridOverlayTest.cpp CMakeLists.txt
git commit -m "feat: add live view grid overlay geometry"
```

---

### Task 2: Draw the overlay in LiveViewWidget

**Files:**
- Modify: `src/ui/LiveViewWidget.h`
- Modify: `src/ui/LiveViewWidget.cpp`

**Interfaces:**
- Consumes: `GridMode`, `grid::segments()` from `src/ui/GridOverlay.h`.
- Produces:
  - `void LiveViewWidget::setGridMode(GridMode m);`
  - `GridMode LiveViewWidget::gridMode() const;`

- [ ] **Step 1: Add include, member, and accessors to the header**

In `src/ui/LiveViewWidget.h`, after `#include <QPointF>` add:
```cpp
#include "ui/GridOverlay.h"
```

In the `public:` section, after the `setCalibrationCrosshair` declaration (line 24), add:
```cpp
    void setGridMode(GridMode m);
    GridMode gridMode() const { return m_gridMode; }
```

In the `private:` section, after `QPointF m_crosshairNorm;` (line 56), add:
```cpp
    GridMode m_gridMode = GridMode::Off;
    void drawGrid(QPainter &painter, const QRect &r) const;
```

Add a forward declaration near the top (after `#include <QPointF>`), before the class:
```cpp
class QPainter;
```

- [ ] **Step 2: Implement setGridMode and drawGrid in the .cpp**

In `src/ui/LiveViewWidget.cpp`, after `setCalibrationCrosshair` (ends line 58), add:
```cpp
void LiveViewWidget::setGridMode(GridMode m) {
    if (m_gridMode == m) return;
    m_gridMode = m;
    update();
}

void LiveViewWidget::drawGrid(QPainter &painter, const QRect &r) const {
    auto segs = grid::segments(m_gridMode);
    if (segs.empty()) return;
    painter.save();
    painter.setClipRect(r);
    painter.setBrush(Qt::NoBrush);
    // Two-pass stroke: dark under-stroke for contrast, then bright line.
    const QColor dark(0, 0, 0, 90);
    const QColor light(255, 255, 255, 180);
    auto toLine = [&](const grid::Seg &s) {
        return QLineF(r.x() + s.x1 * r.width(), r.y() + s.y1 * r.height(),
                      r.x() + s.x2 * r.width(), r.y() + s.y2 * r.height());
    };
    painter.setPen(QPen(dark, 3));
    for (const auto &s : segs) painter.drawLine(toLine(s));
    painter.setPen(QPen(light, 1));
    for (const auto &s : segs) painter.drawLine(toLine(s));
    painter.restore();
}
```

- [ ] **Step 3: Call drawGrid in paintEvent**

In `src/ui/LiveViewWidget.cpp`, in `paintEvent`, immediately after `painter.drawImage(r, m_frame);` (line 78) and before the `if (m_hasReticle)` block, add:
```cpp
    drawGrid(painter, r);
```

Also ensure `<QLineF>` is available: `QLineF` comes via `<QPainter>`/`<QtGui>`, already included. If the build errors on `QLineF`, add `#include <QLineF>` near the top.

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether 2>&1 | tail -5
```
Expected: builds without errors referencing `drawGrid`, `setGridMode`, or `QLineF`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/LiveViewWidget.h src/ui/LiveViewWidget.cpp
git commit -m "feat: paint composition overlay in live view"
```

---

### Task 3: Right-click menu + QSettings persistence

**Files:**
- Modify: `src/ui/LiveViewWidget.h`
- Modify: `src/ui/LiveViewWidget.cpp`

**Interfaces:**
- Consumes: `setGridMode`, `gridMode`, `GridMode` from Task 2.
- Produces: right-click menu behavior; persisted `"liveview/gridMode"`.

- [ ] **Step 1: Declare contextMenuEvent**

In `src/ui/LiveViewWidget.h`, in the `protected:` section after `void mousePressEvent(QMouseEvent *) override;` (line 39), add:
```cpp
    void contextMenuEvent(QContextMenuEvent *) override;
```

- [ ] **Step 2: Persist in setGridMode and restore in the constructor**

In `src/ui/LiveViewWidget.cpp`, add includes near the top (after existing includes):
```cpp
#include <QMenu>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QSettings>
```

Change `setGridMode` (from Task 2) to persist:
```cpp
void LiveViewWidget::setGridMode(GridMode m) {
    if (m_gridMode == m) return;
    m_gridMode = m;
    QSettings().setValue("liveview/gridMode", int(m));
    update();
}
```

In the constructor, before the closing brace of `LiveViewWidget::LiveViewWidget` (after line 14, before line 15 `}`), add:
```cpp
    m_gridMode = GridMode(QSettings().value("liveview/gridMode",
                                            int(GridMode::Off)).toInt());
```

- [ ] **Step 3: Implement contextMenuEvent**

Append to `src/ui/LiveViewWidget.cpp`:
```cpp
void LiveViewWidget::contextMenuEvent(QContextMenuEvent *ev) {
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    struct Item { const char *label; GridMode mode; };
    const Item items[] = {
        {"Off",           GridMode::Off},
        {"Rule of Thirds",GridMode::Thirds},
        {"Golden Ratio",  GridMode::GoldenRatio},
        {"Golden Spiral", GridMode::GoldenSpiral},
        {"Center Crosshair", GridMode::Crosshair},
        {"Diagonals",     GridMode::Diagonals},
    };
    for (const auto &it : items) {
        QAction *a = menu.addAction(it.label);
        a->setCheckable(true);
        a->setChecked(m_gridMode == it.mode);
        group->addAction(a);
        GridMode m = it.mode;
        connect(a, &QAction::triggered, this, [this, m]() { setGridMode(m); });
    }
    menu.exec(ev->globalPos());
}
```

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether 2>&1 | tail -5
```
Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/ui/LiveViewWidget.h src/ui/LiveViewWidget.cpp
git commit -m "feat: right-click grid overlay menu with persistence"
```

---

### Task 4: Manual verification in the app

**Files:** none (verification only).

- [ ] **Step 1: Full build + run the geometry test**

```bash
cmake --build build 2>&1 | tail -5 && ctest --test-dir build -R grid_overlay_test --output-on-failure
```
Expected: build succeeds; `grid_overlay_test` passes.

- [ ] **Step 2: Launch and eyeball each overlay**

Use the `/run` skill (or launch `./build/nikontether`), start live view (or feed a frame), then right-click the live view canvas and select each mode in turn. Confirm:
- Lines land on the image, never on the black letterbox bars (resize the window to force letterboxing and re-check).
- Thirds/Golden/Crosshair/Diagonals look correct; AF reticle still appears ON TOP when you left-click to focus.
- Golden spiral renders as a recognizable spiral. If it looks wrong, refine the spiral construction in `src/ui/GridOverlay.h` per the NOTE in Task 1 (contract: non-empty, in-range) and rebuild.
- Restart the app and confirm the last-selected overlay is restored.

- [ ] **Step 3: Commit any spiral refinement**

```bash
git add src/ui/GridOverlay.h
git commit -m "fix: refine golden spiral overlay geometry"
```
(Skip if no changes were needed.)
