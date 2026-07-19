# Camera Model Preferences — Design

**Date:** 2026-07-19
**Status:** Approved design, pending implementation plan
**Builds on:** [tap-to-focus coordinate fix](2026-07-19-tap-to-focus-coordinate-fix-design.md)

## Problem

The tap-to-focus fix exposes an adjustable AF coordinate frame size via two spin
boxes in `ControlsPanel`, with a single global QSettings value. Users shoot with
different Nikon bodies, and each body needs its own calibrated AF frame. There
is no way to pick a model, and calibration is not remembered per body.

## Goal

Add a **Preferences** dialog (File menu, `Ctrl+,`) with a **camera-model
dropdown** and the AF frame size fields. Selecting a model loads that model's AF
frame; edits are remembered per model. The model auto-selects from the connected
camera, with manual override.

## Chosen approach

- Move the AF frame spin boxes out of `ControlsPanel` into a new
  `PreferencesDialog`.
- Introduce a pure `CameraModels` table (id, display name, default AF frame) plus
  a name matcher for auto-detect.
- Persist AF frame **per model** in QSettings; the dialog is the single writer.
- `RetouchWindow` owns the dialog and routes the AF frame to the live view via a
  new `TetherView::setAfFrameSize` forwarder.

Honest limitation (carried from the base design): libgphoto2 does not expose the
Nikon live-view header, so the built-in per-model defaults are **nominal starting
points** (640 × 426). Real correctness comes from the user calibrating; per-model
memory preserves it.

## Components

### 1. `CameraModels` (src/camera/CameraModels.h, header-only, Qt-light)

```cpp
namespace cammodel {
struct Model {
    const char *id;        // stable settings key, e.g. "d7500"
    const char *display;   // "Nikon D7500"
    int afFrameW;          // nominal default
    int afFrameH;
};
const std::vector<Model>& models();          // ordered list incl. "custom"
const Model* byId(const std::string& id);    // nullptr if unknown
// Match a gphoto2 camera name ("Nikon DSC D7500") to a model id; "" if none.
std::string matchModel(const std::string& cameraName);
}
```

- List: d7500, d750, d780, d850, d500, d5600, d3500, z6, z7, custom.
- All nominal defaults 640 × 426 (documented as starting points).
- `matchModel`: case-insensitive substring match of each model's numeric token
  (e.g. "d7500") in the camera name; falls back to "" (→ "custom" in UI).
- Unit-tested (pure, no Qt).

### 2. `PreferencesDialog` (src/ui/PreferencesDialog.{h,cpp}, QDialog)

State/UI:
- Model `QComboBox` (populated from `cammodel::models()`, display text; id in
  `userData`).
- AF frame width/height `QSpinBox` (range 1..20000).

Behavior:
- On show / construction: read `af/currentModel` (default "custom"), select it,
  and load its AF frame into the spin boxes (see persistence below).
- On model change: load that model's stored AF frame (or its built-in default),
  update spin boxes (signals blocked), persist `af/currentModel`, emit
  `afFrameSizeChanged(w,h)`.
- On spin box change: persist to the current model's per-model keys, emit
  `afFrameSizeChanged(w,h)`.
- Public slot `selectModelById(const QString& id)` for auto-detect; no-op if the
  id is empty or already selected, so a manual override is not clobbered on
  reconnect. (Selecting via auto-detect still loads that model's stored frame.)

Signals:
- `void afFrameSizeChanged(int w, int h);`

Persistence (QSettings, org/app "NikonTether"):
- `af/currentModel` → model id string.
- `af/models/<id>/frameWidth`, `af/models/<id>/frameHeight` → ints.
- Helper `afFrameForModel(id) -> (w,h)`: returns stored per-model value if
  present, else the model's built-in default, else 640 × 426.

Migration: none required. The base design's global `af/frameWidth`/`Height` keys
are superseded; they are simply no longer read. (No user data loss — worst case a
prior global calibration is re-entered once per model.)

### 3. `ControlsPanel` — revert the spin boxes

Remove the AF frame spin boxes, their QSettings I/O, `loadAfFrameSettings`, and
the `afFrameSizeChanged` signal added by the base plan. `ControlsPanel` returns
to camera controls + AF/Capture buttons only.

### 4. `TetherView` — forwarder + model detection

- Add `void setAfFrameSize(int w, int h);` (public) → forwards to `m_liveView`.
- Remove the base plan's `ControlsPanel::afFrameSizeChanged` connection and the
  in-`buildUi` QSettings seeding.
- Add signal `void cameraConnected(const QString& cameraName);`, emitted from
  `handleConnected`, so the host can auto-select the model.

### 5. `RetouchWindow` — menu + ownership + wiring

- Add "Preferences…" to the File menu with shortcut `Ctrl+,`; opens the dialog
  (created once, reused; `show()`/`raise()`/`activateWindow()`).
- `connect(dialog, &PreferencesDialog::afFrameSizeChanged, m_tetherView,
  &TetherView::setAfFrameSize)`.
- `connect(m_tetherView, &TetherView::cameraConnected, this, [dialog](name){
  dialog->selectModelById(matchModel(name)); })`.
- At startup, apply the current model's AF frame to the tether once:
  `m_tetherView->setAfFrameSize(afFrameForModel(currentModel))`.

## Data flow

```
Preferences dialog (model + spin boxes)
   → afFrameSizeChanged(w,h) → TetherView::setAfFrameSize → LiveViewWidget

camera connect → CameraController::connected(name)
   → TetherView::cameraConnected(name)
   → RetouchWindow → dialog.selectModelById(matchModel(name))
   → dialog loads model's AF frame → afFrameSizeChanged → live view

QSettings: af/currentModel, af/models/<id>/frame{Width,Height}
```

## Error handling

- Unknown/empty model id → treated as "custom" (its own per-model memory,
  default 640 × 426).
- Auto-detect no match → leaves current selection unchanged.
- Dialog opened before any camera connects → works; uses persisted current model.

## Testing

- `CameraModels` unit test (extend the CTest executable): `byId` round-trips,
  `matchModel("Nikon DSC D7500") == "d7500"`, `matchModel("Canon EOS") == ""`,
  every listed model has positive default dimensions.
- Manual: open Preferences, switch models, confirm spin boxes reload; edit and
  reopen to confirm per-model persistence; connect the D7500 and confirm it
  auto-selects; confirm the live-view reticle/AF still uses the selected frame.

## Out of scope

- Authoritative per-model AF frame values (still calibrated by the user).
- Non-AF preferences in the dialog (structure allows adding later).
- Reading the model from EXIF or any non-gphoto2 source.
