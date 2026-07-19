# AF Guided Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a guided, convergent AF-frame calibration launched from Preferences: the user clicks a target, the app binary-searches the frame size using three-way "inward / on-target / outward" feedback, per axis, then saves the result per model.

**Architecture:** A pure `AfCalibrator` binary-search state machine (Qt-free, tested) drives the search. `LiveViewWidget` gains a calibration mode (crosshair + `calibrationPointPicked` signal). `TetherView` orchestrates an overlay panel, AF commands, and feedback, emitting `calibrationFinished(w,h)`. `PreferencesDialog` adds a Calibrate… button; `RetouchWindow` switches to the Tether page, runs calibration, and persists the result per model.

**Tech Stack:** C++17, Qt6 (Widgets), CMake. QSettings scope org/app "NikonTether".

## Global Constraints

- C++17. New sources → `nikontether` target; new test executables → `add_executable` + `add_test`.
- Search: tolerance `tol = 16`, iteration cap `12` per axis. Default bounds: width [200, 3000], height [150, 2200].
- Direction convention (monotonic): frame too small → focus **inward** (toward center) → raise the low bound; too large → focus **outward** (toward edge) → lower the high bound; correct → **on-target**.
- Per-model persistence keys (existing): `af/models/<id>/frameWidth`, `af/models/<id>/frameHeight`, `af/currentModel`.
- Follow existing patterns: `RetouchWindow::setMode(Mode::Tether)` switches pages; live-view active state is `m_liveViewAction->isChecked()` inside `TetherView`.

---

### Task 1: AfCalibrator state machine (pure) + test

**Files:**
- Create: `src/camera/AfCalibrator.h`
- Create: `tests/AfCalibratorTest.cpp`
- Modify: `CMakeLists.txt` (add test target after `cam_models_test`)

**Interfaces:**
- Consumes: nothing.
- Produces: `class AfCalibrator` with `enum class Axis { Width, Height };`, `enum class Feedback { Inward, OnTarget, Outward };`, and methods:
  - `void begin(int loW, int hiW, int loH, int hiH);`
  - `void setTarget(double normX, double normY);`
  - `Axis axis() const;` / `bool done() const;`
  - `int currentGuess() const;`
  - `void afCommand(int otherW, int otherH, int &afX, int &afY) const;`
  - `bool applyFeedback(Feedback f);` (returns true when the active axis converged this step)
  - `void nextAxis();`
  - `int resultW() const;` / `int resultH() const;`

- [ ] **Step 1: Write the failing test**

Create `tests/AfCalibratorTest.cpp`:

```cpp
#include "camera/AfCalibrator.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

// Simulate the user's feedback for a known true frame size on one axis.
static AfCalibrator::Feedback judge(int guess, int truth) {
    if (guess < truth - 4) return AfCalibrator::Feedback::Inward;
    if (guess > truth + 4) return AfCalibrator::Feedback::Outward;
    return AfCalibrator::Feedback::OnTarget;
}

static void runOne(int trueW, int trueH) {
    AfCalibrator c;
    c.begin(200, 3000, 150, 2200);

    // Width axis.
    c.setTarget(0.9, 0.5);
    int rounds = 0;
    while (true) {
        assert(++rounds <= 12);
        bool converged = c.applyFeedback(judge(c.currentGuess(), trueW));
        if (converged) break;
    }
    c.nextAxis();
    assert(!c.done());

    // Height axis.
    c.setTarget(0.5, 0.9);
    rounds = 0;
    while (true) {
        assert(++rounds <= 12);
        bool converged = c.applyFeedback(judge(c.currentGuess(), trueH));
        if (converged) break;
    }
    c.nextAxis();
    assert(c.done());

    assert(std::abs(c.resultW() - trueW) <= 16);
    assert(std::abs(c.resultH() - trueH) <= 16);
}

int main() {
    runOne(500, 400);
    runOne(900, 700);
    runOne(1500, 1100);
    runOne(640, 426);

    // afCommand maps target * guess for the active axis, target * other for the
    // passive axis.
    {
        AfCalibrator c;
        c.begin(200, 3000, 150, 2200);
        c.setTarget(0.5, 0.5);
        int gx = c.currentGuess(); // width guess = mid(200,3000)=1600
        int afx = 0, afy = 0;
        c.afCommand(1000, 800, afx, afy);
        assert(afx == (int)(0.5 * gx + 0.5));
        assert(afy == (int)(0.5 * 800 + 0.5));
    }

    std::puts("AfCalibratorTest: all assertions passed");
    return 0;
}
```

- [ ] **Step 2: Add the CMake test target**

In `CMakeLists.txt`, after the `add_test(NAME cam_models_test ...)` line:

```cmake
add_executable(af_calibrator_test tests/AfCalibratorTest.cpp)
target_include_directories(af_calibrator_test PRIVATE src)
add_test(NAME af_calibrator_test COMMAND af_calibrator_test)
```

- [ ] **Step 3: Run the test to verify it fails to build**

Run:
```bash
cmake -S . -B build >/dev/null && cmake --build build --target af_calibrator_test
```
Expected: FAIL — `camera/AfCalibrator.h` not found.

- [ ] **Step 4: Write the implementation**

Create `src/camera/AfCalibrator.h`:

```cpp
#pragma once

// Pure, Qt-free binary-search calibrator for the AF coordinate frame size.
// One axis at a time; the caller feeds three-way user feedback. See the design
// doc for the monotonic direction argument (too small -> inward; too big ->
// outward; correct -> on-target).
class AfCalibrator {
public:
    enum class Axis { Width, Height };
    enum class Feedback { Inward, OnTarget, Outward };

    void begin(int loW, int hiW, int loH, int hiH) {
        m_loW = loW; m_hiW = hiW; m_loH = loH; m_hiH = hiH;
        m_axis = Axis::Width;
        m_done = false;
        m_targetX = m_targetY = 0.5;
        m_resultW = (loW + hiW) / 2;
        m_resultH = (loH + hiH) / 2;
    }

    void setTarget(double normX, double normY) {
        m_targetX = normX;
        m_targetY = normY;
    }

    Axis axis() const { return m_axis; }
    bool done() const { return m_done; }

    int currentGuess() const {
        return m_axis == Axis::Width ? (m_loW + m_hiW) / 2 : (m_loH + m_hiH) / 2;
    }

    void afCommand(int otherW, int otherH, int &afX, int &afY) const {
        int g = currentGuess();
        int w = m_axis == Axis::Width ? g : otherW;
        int h = m_axis == Axis::Height ? g : otherH;
        afX = clampRound(m_targetX * w, w);
        afY = clampRound(m_targetY * h, h);
    }

    // Returns true if the active axis converged this step.
    bool applyFeedback(Feedback f) {
        int &lo = (m_axis == Axis::Width) ? m_loW : m_loH;
        int &hi = (m_axis == Axis::Width) ? m_hiW : m_hiH;
        int &result = (m_axis == Axis::Width) ? m_resultW : m_resultH;
        int guess = (lo + hi) / 2;

        if (f == Feedback::OnTarget) {
            result = guess;
            return true;
        }
        if (f == Feedback::Inward) lo = guess;   // too small -> raise floor
        else                       hi = guess;   // Outward: too big -> lower ceiling

        int mid = (lo + hi) / 2;
        if (hi - lo <= kTol || ++m_iter >= kCap) {
            result = mid;
            m_iter = 0;
            return true;
        }
        return false;
    }

    void nextAxis() {
        if (m_axis == Axis::Width) {
            m_axis = Axis::Height;
            m_iter = 0;
        } else {
            m_done = true;
        }
    }

    int resultW() const { return m_resultW; }
    int resultH() const { return m_resultH; }

private:
    static constexpr int kTol = 16;
    static constexpr int kCap = 12;

    static int clampRound(double v, int hi) {
        int r = int(v + 0.5);
        if (r < 0) r = 0;
        if (r > hi) r = hi;
        return r;
    }

    int m_loW = 200, m_hiW = 3000, m_loH = 150, m_hiH = 2200;
    int m_resultW = 1600, m_resultH = 1175;
    Axis m_axis = Axis::Width;
    bool m_done = false;
    int m_iter = 0;
    double m_targetX = 0.5, m_targetY = 0.5;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cmake --build build --target af_calibrator_test && ctest --test-dir build -R af_calibrator_test --output-on-failure
```
Expected: PASS — `AfCalibratorTest: all assertions passed`.

- [ ] **Step 6: Commit**

```bash
git add src/camera/AfCalibrator.h tests/AfCalibratorTest.cpp CMakeLists.txt
git commit -m "feat: add AF calibration binary-search state machine with test"
```

---

### Task 2: LiveViewWidget calibration mode

**Files:**
- Modify: `src/ui/LiveViewWidget.h`
- Modify: `src/ui/LiveViewWidget.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `void LiveViewWidget::setCalibrationMode(bool on);`
  - `void LiveViewWidget::setCalibrationCrosshair(bool on, QPointF norm = {});`
  - `signal void LiveViewWidget::calibrationPointPicked(double normX, double normY);`

- [ ] **Step 1: Update the header**

In `src/ui/LiveViewWidget.h`, add to the public section (after `void setAfFrameSize(int w, int h);`):

```cpp
    // Calibration mode: clicks emit calibrationPointPicked instead of firing AF,
    // and a crosshair is drawn at the target position.
    void setCalibrationMode(bool on);
    void setCalibrationCrosshair(bool on, QPointF norm = {});
```

Add to `signals:` (after `void focusRequested(int sensorX, int sensorY);`):

```cpp
    void calibrationPointPicked(double normX, double normY);
```

Add to the private members (after `double m_afBoxFrac = 0.12;`):

```cpp
    bool m_calibrating = false;
    bool m_hasCrosshair = false;
    QPointF m_crosshairNorm;
```

- [ ] **Step 2: Implement the setters**

In `src/ui/LiveViewWidget.cpp`, after `clearReticle()`:

```cpp
void LiveViewWidget::setCalibrationMode(bool on) {
    m_calibrating = on;
    if (!on) {
        m_hasCrosshair = false;
    } else {
        // Hide the normal AF reticle while calibrating.
        m_hasReticle = false;
    }
    update();
}

void LiveViewWidget::setCalibrationCrosshair(bool on, QPointF norm) {
    m_hasCrosshair = on;
    m_crosshairNorm = norm;
    update();
}
```

- [ ] **Step 3: Route clicks in calibration mode**

In `src/ui/LiveViewWidget.cpp`, at the very start of `mousePressEvent`, before the AF logic, add a calibration branch. Replace the opening of the method:

```cpp
void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;
```

with:

```cpp
void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;

    if (m_calibrating) {
        double nx = double(ev->pos().x() - r.x()) / r.width();
        double ny = double(ev->pos().y() - r.y()) / r.height();
        m_crosshairNorm = QPointF(nx, ny);
        m_hasCrosshair = true;
        update();
        emit calibrationPointPicked(nx, ny);
        return;
    }
```

- [ ] **Step 4: Draw the crosshair**

In `src/ui/LiveViewWidget.cpp` `paintEvent`, after the reticle block (inside the `if (!m_frame.isNull())` painting path, i.e. after the `if (m_hasReticle) { ... }` block), add:

```cpp
    if (m_hasCrosshair) {
        QPointF c(r.x() + m_crosshairNorm.x() * r.width(),
                  r.y() + m_crosshairNorm.y() * r.height());
        QPen pen(QColor(80, 180, 255), 2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        int rad = 14;
        painter.drawEllipse(c, rad, rad);
        painter.drawLine(QPointF(c.x() - rad - 6, c.y()), QPointF(c.x() + rad + 6, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - rad - 6), QPointF(c.x(), c.y() + rad + 6));
    }
```

- [ ] **Step 5: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/LiveViewWidget.h src/ui/LiveViewWidget.cpp
git commit -m "feat: add calibration mode with crosshair to live view widget"
```

---

### Task 3: TetherView calibration orchestration + overlay panel

**Files:**
- Modify: `src/ui/TetherView.h`
- Modify: `src/ui/TetherView.cpp`

**Interfaces:**
- Consumes: `AfCalibrator` (Task 1), `LiveViewWidget` calibration API (Task 2), `CameraController::setAfArea`.
- Produces:
  - `void TetherView::startCalibration();` (public)
  - `signal void TetherView::calibrationFinished(int w, int h);`

- [ ] **Step 1: Header — includes, members, API**

In `src/ui/TetherView.h`:

Add the calibrator include at the top (after the existing includes):

```cpp
#include "camera/AfCalibrator.h"
```

Add forward decls near the other `class` forwards:

```cpp
class QFrame;
class QLabel;
class QPushButton;
```

Add to public (after `void setAfFrameSize(int w, int h);`):

```cpp
    // Begin guided AF-frame calibration (no-op unless live view is running).
    void startCalibration();
```

Add to `signals:` (after `void cameraConnected(const QString &cameraName);`):

```cpp
    void calibrationFinished(int w, int h);
```

Add private helpers and members (near `void updateCaptureShortcut();` and the member block):

```cpp
    void buildCalibrationPanel();
    void positionCalibrationPanel();
    void fireCalibrationAf();
    void updateCalibrationPrompt();
    void endCalibration(bool finished);

    AfCalibrator m_calibrator;
    bool m_calibrating = false;
    int m_afFrameW = 640;
    int m_afFrameH = 426;

    QFrame *m_calibPanel = nullptr;
    QLabel *m_calibLabel = nullptr;
    QPushButton *m_calibInward = nullptr;
    QPushButton *m_calibOn = nullptr;
    QPushButton *m_calibOutward = nullptr;
    QPushButton *m_calibRefire = nullptr;
    QPushButton *m_calibCancel = nullptr;
```

- [ ] **Step 2: Track applied AF frame size**

In `src/ui/TetherView.cpp`, update `setAfFrameSize` to remember the values:

```cpp
void TetherView::setAfFrameSize(int w, int h) {
    m_afFrameW = w;
    m_afFrameH = h;
    m_liveView->setAfFrameSize(w, h);
}
```

- [ ] **Step 3: Add includes and connect the calibration click**

In `src/ui/TetherView.cpp`, add includes near the top:

```cpp
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
```

(`QLabel` / `QVBoxLayout` may already be included — do not duplicate; check first.)

In the constructor, after the `afAreaResult` connection, add:

```cpp
    connect(m_liveView, &LiveViewWidget::calibrationPointPicked, this,
            [this](double nx, double ny) {
                if (!m_calibrating) return;
                m_calibrator.setTarget(nx, ny);
                fireCalibrationAf();
            });
```

- [ ] **Step 4: Build the calibration panel**

In `src/ui/TetherView.cpp`, add the panel builder (call it lazily from `startCalibration`). The panel is a child of `m_liveView` so it floats over the live view:

```cpp
void TetherView::buildCalibrationPanel() {
    if (m_calibPanel) return;
    m_calibPanel = new QFrame(m_liveView);
    m_calibPanel->setFrameShape(QFrame::StyledPanel);
    m_calibPanel->setAutoFillBackground(true);
    m_calibPanel->setStyleSheet(
        "QFrame { background: rgba(20,20,20,220); border-radius: 8px; }"
        "QLabel { color: white; }");

    auto *v = new QVBoxLayout(m_calibPanel);
    m_calibLabel = new QLabel;
    m_calibLabel->setWordWrap(true);
    m_calibLabel->setMinimumWidth(360);
    v->addWidget(m_calibLabel);

    auto *row = new QHBoxLayout;
    m_calibInward = new QPushButton("Focused inward\n(toward center)");
    m_calibOn = new QPushButton("On the target");
    m_calibOutward = new QPushButton("Focused outward\n(toward edge)");
    row->addWidget(m_calibInward);
    row->addWidget(m_calibOn);
    row->addWidget(m_calibOutward);
    v->addLayout(row);

    auto *row2 = new QHBoxLayout;
    m_calibRefire = new QPushButton("Re-fire");
    m_calibCancel = new QPushButton("Cancel");
    row2->addStretch(1);
    row2->addWidget(m_calibRefire);
    row2->addWidget(m_calibCancel);
    v->addLayout(row2);

    auto feedback = [this](AfCalibrator::Feedback f) {
        if (!m_calibrating) return;
        bool converged = m_calibrator.applyFeedback(f);
        if (!converged) { fireCalibrationAf(); updateCalibrationPrompt(); return; }
        m_calibrator.nextAxis();
        if (m_calibrator.done()) {
            int w = m_calibrator.resultW(), h = m_calibrator.resultH();
            endCalibration(true);
            emit calibrationFinished(w, h);
        } else {
            // Height axis: wait for a fresh click near the top/bottom edge.
            m_liveView->setCalibrationCrosshair(false);
            updateCalibrationPrompt();
        }
    };
    connect(m_calibInward, &QPushButton::clicked, this,
            [feedback] { feedback(AfCalibrator::Feedback::Inward); });
    connect(m_calibOn, &QPushButton::clicked, this,
            [feedback] { feedback(AfCalibrator::Feedback::OnTarget); });
    connect(m_calibOutward, &QPushButton::clicked, this,
            [feedback] { feedback(AfCalibrator::Feedback::Outward); });
    connect(m_calibRefire, &QPushButton::clicked, this,
            [this] { if (m_calibrating) fireCalibrationAf(); });
    connect(m_calibCancel, &QPushButton::clicked, this,
            [this] { endCalibration(false); });
}

void TetherView::positionCalibrationPanel() {
    if (!m_calibPanel) return;
    m_calibPanel->adjustSize();
    int x = (m_liveView->width() - m_calibPanel->width()) / 2;
    m_calibPanel->move(qMax(0, x), 12);
    m_calibPanel->raise();
}
```

- [ ] **Step 5: Start / fire / prompt / end**

Add in `src/ui/TetherView.cpp`:

```cpp
void TetherView::startCalibration() {
    if (!m_liveViewAction->isChecked()) {
        emit statusMessage("Start Live View before calibrating.");
        return;
    }
    buildCalibrationPanel();
    m_viewTabs->setCurrentWidget(m_liveView);
    m_calibrating = true;
    m_calibrator.begin(200, 3000, 150, 2200);
    m_liveView->setCalibrationMode(true);
    m_liveView->setCalibrationCrosshair(false);
    updateCalibrationPrompt();
    m_calibPanel->show();
    positionCalibrationPanel();
}

void TetherView::fireCalibrationAf() {
    int afx = 0, afy = 0;
    m_calibrator.afCommand(m_afFrameW, m_afFrameH, afx, afy);
    m_controller->setAfArea(afx, afy);
    updateCalibrationPrompt();
}

void TetherView::updateCalibrationPrompt() {
    if (!m_calibLabel) return;
    const bool widthAxis = m_calibrator.axis() == AfCalibrator::Axis::Width;
    const QString axis = widthAxis ? "WIDTH" : "HEIGHT";
    const QString edge = widthAxis ? "left or right" : "top or bottom";
    m_calibLabel->setText(QString(
        "<b>Calibrating %1</b><br>"
        "Aim at a scene with depth across the frame (e.g. a tape measure "
        "receding from the camera). Click your target near the %2 edge, then "
        "watch which part of the live view snaps sharp and choose below. "
        "Guess: %3.")
        .arg(axis, edge)
        .arg(m_calibrator.currentGuess()));
    positionCalibrationPanel();
}

void TetherView::endCalibration(bool /*finished*/) {
    m_calibrating = false;
    m_liveView->setCalibrationMode(false);
    m_liveView->setCalibrationCrosshair(false);
    if (m_calibPanel) m_calibPanel->hide();
}
```

- [ ] **Step 6: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/TetherView.h src/ui/TetherView.cpp
git commit -m "feat: orchestrate guided AF calibration in TetherView with overlay panel"
```

---

### Task 4: Preferences button + RetouchWindow wiring + persistence

**Files:**
- Modify: `src/ui/PreferencesDialog.h`
- Modify: `src/ui/PreferencesDialog.cpp`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `TetherView::startCalibration`, `TetherView::calibrationFinished`, `RetouchWindow::setMode`.
- Produces:
  - `signal void PreferencesDialog::calibrationRequested();`
  - `void PreferencesDialog::setAfFrame(int w, int h);` (push calibrated values into the spin boxes and persist for the current model)

- [ ] **Step 1: PreferencesDialog header**

In `src/ui/PreferencesDialog.h`:

Add to public (after `void selectModelById(const QString &id);`):

```cpp
    // Push calibrated values into the fields and persist them for the model.
    void setAfFrame(int w, int h);
```

Add to `signals:` (after `void afFrameSizeChanged(int w, int h);`):

```cpp
    void calibrationRequested();
```

Add a forward decl `class QPushButton;` near the other forwards, and a member `QPushButton *m_calibrate = nullptr;` in the private section.

- [ ] **Step 2: PreferencesDialog implementation**

In `src/ui/PreferencesDialog.cpp`:

Add `#include <QPushButton>` with the other includes.

In the constructor, after the AF frame spin-box rows are added to `form`, add the button:

```cpp
    m_calibrate = new QPushButton("Calibrate…");
    form->addRow(QString(), m_calibrate);
    connect(m_calibrate, &QPushButton::clicked, this,
            [this] { emit calibrationRequested(); });
```

Add the setter at the end of the file:

```cpp
void PreferencesDialog::setAfFrame(int w, int h) {
    {
        QSignalBlocker bw(m_frameW);
        QSignalBlocker bh(m_frameH);
        m_frameW->setValue(w);
        m_frameH->setValue(h);
    }
    onFrameEdited(); // persist for the current model + emit afFrameSizeChanged
}
```

- [ ] **Step 3: RetouchWindow wiring**

In `src/edit/RetouchWindow.cpp`, in the block where `m_prefsDialog` is created and connected (added in the previous plan), append:

```cpp
    connect(m_prefsDialog, &PreferencesDialog::calibrationRequested, this,
            [this] {
                m_prefsDialog->hide();
                setMode(Mode::Tether);
                m_tetherView->startCalibration();
            });
    connect(m_tetherView, &TetherView::calibrationFinished, this,
            [this](int w, int h) {
                m_prefsDialog->setAfFrame(w, h);
                m_statusLabel->setText(
                    QString("Calibration saved: AF frame %1 × %2").arg(w).arg(h));
            });
```

- [ ] **Step 4: Build, test, smoke-launch**

Run:
```bash
cmake --build build --target nikontether && ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 3 ./build/nikontether >/tmp/nt.log 2>&1; echo "exit=$? (124=ok)"
```
Expected: build PASS; all tests pass; app stays up (exit 124).

- [ ] **Step 5: Manual verification + commit**

Manual (GUI, with D7500 in live view): open Preferences → Calibrate…; the app
switches to Tether with the overlay panel; set up a receding tape measure; click
the target near an edge; AF fires; pick inward/on/outward; confirm it converges,
switches to the height axis, finishes, and the saved value persists for the
D7500 (reopen Preferences to confirm).

Commit:
```bash
git add src/ui/PreferencesDialog.h src/ui/PreferencesDialog.cpp src/edit/RetouchWindow.cpp
git commit -m "feat: add Calibrate button and wire guided calibration end-to-end"
```

---

## Notes for the implementer

- `AfCalibrator` is a plain value type (no QObject); `TetherView` owns one by value.
- The calibration panel is a child of `m_liveView`, so it floats over the live
  view and is centered near the top; it is repositioned whenever the prompt
  updates. It does not auto-reflow on live-view resize mid-calibration — acceptable
  for a short interactive flow.
- During the width axis, `fireCalibrationAf` uses the current `m_afFrameH` for the
  passive (height) coordinate, and vice versa — good enough since only the active
  axis is being solved and the target sits mid-frame on the passive axis.
- No `qRegisterMetaType` needed: all new signals use built-in types on direct
  (same-thread) connections.
