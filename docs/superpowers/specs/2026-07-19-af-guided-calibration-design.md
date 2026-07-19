# AF Guided Calibration — Design

**Date:** 2026-07-19
**Status:** Proposed, pending approval
**Builds on:** [camera model preferences](2026-07-19-camera-model-preferences-design.md),
[tap-to-focus coordinate fix](2026-07-19-tap-to-focus-coordinate-fix-design.md)

## Problem

The AF coordinate frame size can't be read from the camera (libgphoto2 strips the
Nikon live-view header), so tap-to-focus currently needs the user to guess the
frame width/height by nudging spin boxes. We want a **guided, convergent**
calibration instead of blind nudging.

## Key idea

Tap-to-focus maps a click to `afX = normX × frameW` (and similarly y). The
physical spot the camera focuses is at FOV fraction `normX × frameW / trueW`.
So:

- `frameW` too small → focus lands **inward** (between the clicked object and
  center).
- `frameW` correct → focus lands **on** the object.
- `frameW` too large → focus lands **outward** (past the object toward the edge).

This is monotonic, so a **binary search** on `frameW` driven by three-way user
feedback (inward / on-target / outward) converges in ~6 rounds. Height is
calibrated the same way on a separate axis. Center is always correct, so only
the outward scaling needs solving.

Feedback signal: the user judges, from **live-view sharpness**, where focus
landed. Requires a scene with depth varying across the frame (e.g. a tape
measure receding from the camera); this is stated in the setup instructions.

## Components

### 1. `AfCalibrator` (src/camera/AfCalibrator.{h,cpp} or header-only, QObject)

Pure state machine for one axis at a time. Unit-testable (no Qt widgets).

```cpp
class AfCalibrator {
public:
    enum class Axis { Width, Height };
    enum class Feedback { Inward, OnTarget, Outward };

    void begin(int loW, int hiW, int loH, int hiH); // search bounds per axis
    // Set the clicked object position (normalized 0..1) for the active axis.
    void setTarget(double normX, double normY);
    int  currentGuess() const;      // current frame size for the active axis
    Axis axis() const;
    bool done() const;

    // Compute the AF command for the current guess + stored target, using
    // `otherAxisValue` for the non-active axis.
    void afCommand(int otherW, int otherH, int &afX, int &afY) const;

    // Advance the search with the user's judgement; returns true if the active
    // axis converged this step (caller then switches axis or finishes).
    bool applyFeedback(Feedback f);

    int resultW() const;
    int resultH() const;
};
```

Search: bisect `[lo,hi]`; `Inward` → `lo = guess`; `Outward` → `hi = guess`;
`OnTarget` → lock the axis at `guess`. Stop an axis after convergence
(`hi - lo <= tol`, tol ≈ 8) even without OnTarget, taking the midpoint. Default
bounds: width [200, 3000], height [150, 2200] (generous around the ~640×426
nominal). Iterations capped (e.g. 9) as a safety net.

### 2. `LiveViewWidget` — calibration overlay mode

- `void setCalibrationCrosshair(bool on, QPointF norm = {});` — draws a crosshair
  + ring at the normalized position while calibrating.
- A mode flag `m_calibrating`. When set, a click emits a new signal
  `calibrationPointPicked(double normX, double normY)` instead of
  `focusRequested`, and does not arm the normal reticle.
- Reuses `drawnRect()` for click→normalized mapping (same as normal clicks).

### 3. Calibration UI panel (in `TetherView`)

A small overlay panel (a `QFrame` child of the live-view area, or a floating
widget) shown during calibration:

- Instruction label (setup text, then per-round prompt).
- Three feedback buttons: **Focused inward (toward center)**,
  **On the target**, **Focused outward (toward edge)**.
- **Re-fire** (repeat AF at current guess) and **Cancel** buttons.
- Progress hint ("Width — round 3", "Height — round 2").

`TetherView` orchestrates:
- `void startCalibration();` — enters calibration: `m_liveView` mode on, panel
  shown, `AfCalibrator::begin(...)`, prompt for the width-axis click.
- On `calibrationPointPicked`: `calibrator.setTarget(...)`, fire AF via
  `afCommand` + `CameraController::setAfArea`, show feedback buttons.
- On a feedback button: `calibrator.applyFeedback(...)`; if the axis converged,
  switch to the height axis (prompt for a new click) or, if both done, finish.
- Finish: hide panel, exit live-view calibration mode, emit
  `calibrationFinished(int w, int h)`.
- Cancel: restore normal mode, emit nothing.
- Guard: `startCalibration()` is a no-op (with a status message) unless
  connected and live view is active.

### 4. Preferences + RetouchWindow wiring

- `PreferencesDialog`: add a **Calibrate…** button next to the AF frame fields.
  It emits `calibrationRequested()`. Disable it with a tooltip when not usable
  (the dialog doesn't know camera state, so it always emits; TetherView guards).
- `RetouchWindow`: on `PreferencesDialog::calibrationRequested`, hide the dialog,
  switch to the Tether page (`setMode(Tether)` equivalent), and call
  `m_tetherView->startCalibration()`.
- On `TetherView::calibrationFinished(w,h)`: write the values to the current
  model's per-model QSettings keys, push them into the live `PreferencesDialog`
  spin boxes, and apply via `m_tetherView->setAfFrameSize(w,h)`. Also offer (via
  a `QMessageBox`) to store them as the model's session default — implemented
  simply by the per-model save, which already persists; no separate default
  needed. (Drop the "built-in default" offer — per-model persistence covers it.)

## Data flow (one axis)

```
Preferences "Calibrate…" → RetouchWindow → switch to Tether + TetherView::startCalibration
  → LiveViewWidget calibration mode + panel shown
  → user clicks object → calibrationPointPicked(normX,normY)
  → AfCalibrator.setTarget + afCommand → CameraController::setAfArea (AF fires)
  → user judges sharpness → feedback button → AfCalibrator.applyFeedback
      ├─ not converged → re-fire AF with new guess
      ├─ axis converged, more axes → prompt next-axis click
      └─ all done → calibrationFinished(w,h) → RetouchWindow persists per model
```

## Error handling

- Not connected / live view off → `startCalibration()` shows a status message and
  returns; Preferences stays open.
- Cancel at any point → restore normal live-view mode, no settings change.
- Iteration cap reached without OnTarget → take current midpoint, finish the axis
  (result is still within tolerance).
- AF command failure (`afAreaResult(false)`) during calibration → show a
  message in the panel and let the user re-fire.

## Testing

- `AfCalibrator` unit test (extend CTest): simulate a known `trueW`; feed
  feedback computed from `guess` vs `trueW`; assert convergence to within
  tolerance in ≤ 9 rounds for several `trueW` values (e.g. 500, 900, 1500);
  assert bounds are respected and axis switching works.
- Manual: run calibration on the D7500 with a receding tape measure; confirm
  convergence, that saved values persist per model, and that subsequent
  tap-to-focus lands correctly at the edges.

## Out of scope

- Reading AF position from the camera (still impossible via libgphoto2).
- Auto-detecting scene depth or automating the judgement (user provides it).
- Calibrating rotation/skew (assumes axis-aligned linear scaling only).
