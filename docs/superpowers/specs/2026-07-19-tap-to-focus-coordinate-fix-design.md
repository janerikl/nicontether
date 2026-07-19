# Tap-to-Focus Coordinate Fix — Design

**Date:** 2026-07-19
**Status:** Approved design, pending implementation plan

## Problem

Clicking a spot in the live view is supposed to make the camera autofocus on
that spot. The end-to-end wiring already exists:

```
LiveViewWidget click
  → focusRequested(x, y)
  → CameraController::setAfArea
  → CameraWorker::setAfArea  (gphoto2 "changeafarea" + "autofocusdrive")
```

But the camera focuses on the **wrong spot**. The click is close to correct at
the image center and drifts off toward the edges.

## Root cause

`LiveViewWidget::mousePressEvent` (src/ui/LiveViewWidget.cpp:44-54) scales the
click to the **decoded preview JPEG's own pixel dimensions**
(`m_frame.width()/height()`) and emits those as the AF coordinates.

Nikon's `ChangeAfArea` PTP op (`0x9205`) — which libgphoto2's `changeafarea`
calls verbatim (`config.c:_put_Nikon_ChangeAfArea` → `ptp_nikon_changeafarea`,
passing x,y straight through) — expects coordinates in the **AF coordinate
frame** carried by the Nikon live-view header's `ImageWidth × ImageHeight`
fields (header byte offsets 4–5 / 6–7, little-endian).

Reference implementation (digiCamControl `NikonBase.cs` +
`LiveViewViewModel.cs`) maps a click as:

```
posX = clickX_in_displayed_frame × (ImageWidth  / displayedFrameWidth)
posY = clickY_in_displayed_frame × (ImageHeight / displayedFrameHeight)
```

The AF frame (`ImageWidth × ImageHeight`) is a **different scale** from the
decoded JPEG. libgphoto2's high-level preview path **discards the 128-byte Nikon
header** (`library.c:3853`, `FIXME: perhaps handle the 128 byte header data
too`), so imgcapture never receives `ImageWidth`/`ImageHeight` and cannot derive
the scale from the frame alone. Using the JPEG's own dimensions is therefore
only right by accident — and here it is wrong.

Because the displayed JPEG and the AF frame both cover the **full** live-view
field of view, the center of the image always maps to the center of the AF frame
regardless of scale. The scale error only appears away from center, growing
toward the edges — which matches the observed symptom and makes manual
edge-based calibration practical.

## Chosen approach

**Per-model AF-frame constant, with a persisted adjustable setting.** No changes
to the camera/gphoto2 layer.

The mapping needs only the AF coordinate frame size, not the JPEG size:

```
normX = (clickX − drawnRect.x) / drawnRect.width      // 0..1 within the image
normY = (clickY − drawnRect.y) / drawnRect.height
afX   = round(normX × afFrameWidth)
afY   = round(normY × afFrameHeight)
```

`afFrameWidth`/`afFrameHeight` come from a persisted setting, seeded with a
built-in default for the connected Nikon family (target body: D750 / D7x00 /
D5x00). The user can adjust the values to calibrate: click a corner, see where
AF lands, nudge the frame size until the reticle and actual focus agree. Center
is correct immediately; only edge scaling is tuned.

Rejected alternatives:
- *Parse the header ourselves* (raw PTP `GetLiveViewImage 0x9203`): exact and
  model-independent, but a large rewrite of the camera layer bypassing
  libgphoto2's preview API. Out of scope.
- *Automatic empirical calibration*: not possible — reading where AF actually
  landed (`FocusX/FocusY`) also needs the discarded header.

## Components

### 1. `LiveViewWidget` (src/ui/LiveViewWidget.{h,cpp})

New state:
- `int m_afFrameW = 0, m_afFrameH = 0;` — AF coordinate frame size. When either
  is ≤ 0, fall back to the decoded frame dimensions (current behavior), so the
  widget is safe before a size is set.
- Reticle: `bool m_hasReticle = false;` `QPointF m_reticleNorm;` (0..1 position),
  and `enum class AfState { Pending, Ok, Failed } m_afState;`.
- `double m_afBoxFrac = 0.12;` — reticle box size as a fraction of the drawn
  image's shorter side (the header's real box size is unavailable; this is a
  display-only constant).

New API:
- `void setAfFrameSize(int w, int h);`
- `void setAfResult(bool ok);` — flips reticle Pending → Ok/Failed.
- `void clearReticle();`

`mousePressEvent`: reject clicks outside `drawnRect()` (unchanged); compute
`normX/normY`; emit `focusRequested(round(normX×afW), round(normY×afH))` using
the AF frame size (or frame-size fallback); store `m_reticleNorm`, set
`m_afState = Pending`, `m_hasReticle = true`; `update()`.

`paintEvent`: after drawing the frame, if `m_hasReticle` draw a square centered
at `m_reticleNorm` mapped into `drawnRect()`, side = `m_afBoxFrac × min(drawn
w,h)`, pen colored by state (Pending = yellow, Ok = green, Failed = red), 2px.

### 2. AF result feedback (camera layer)

`CameraWorker::setAfArea` currently returns void and calls `triggerAutofocus`.
Add a signal reporting command acceptance so the reticle can turn green/red:

- `CameraWorker`: emit `afAreaResult(bool ok)` — `false` when `changeafarea`
  widget is absent or `gp_camera_set_config` fails; `true` when the set + AF
  drive commands return `GP_OK`.
- `CameraController`: re-emit `afAreaResult(bool)` (queued, like other worker
  signals).
- `TetherView`: `connect(m_controller, &CameraController::afAreaResult,
  m_liveView, &LiveViewWidget::setAfResult)`.

Scope note: `GP_OK` means "command accepted," not "subject in focus" (true
in-focus state lives in the discarded header). Green = AF command succeeded, not
a focus-confirm. This is a deliberate, documented limitation.

### 3. Settings + calibration UI

- Persist under the existing QSettings scope (org/app "NikonTether"):
  `af/frameWidth`, `af/frameHeight` (ints).
- Defaults: built-in constant for the D750/D7x00/D5x00 family, applied when no
  stored value exists. The exact default is a starting point; the setting is the
  correctness mechanism.
- UI: a compact "Focus area calibration" control in `ControlsPanel` (near the
  existing Autofocus button) — two spin boxes (AF frame width / height) wired to
  save the QSettings values and call `LiveViewWidget::setAfFrameSize`. On
  connect / first frame, `TetherView` seeds the widget from settings.

## Data flow (after fix)

```
click → LiveViewWidget (normX,normY × afFrame) → focusRequested(afX,afY)
      → CameraController::setAfArea → CameraWorker::setAfArea
          → gp changeafarea + autofocusdrive → afAreaResult(ok)
      → CameraController → LiveViewWidget::setAfResult(ok) → reticle color

settings spin boxes → af/frameWidth,Height (QSettings)
                    → LiveViewWidget::setAfFrameSize
```

## Error handling

- `changeafarea` widget absent (non-Nikon / unsupported body): worker logs
  "Focus-point selection not available on this camera." (existing) and emits
  `afAreaResult(false)` → red reticle.
- Click outside the drawn image: ignored (existing guard).
- AF frame size unset/invalid: fall back to decoded-frame dimensions.

## Testing

- Unit-testable mapping: extract the normalized→AF-coord math so a test can
  assert center→(afW/2, afH/2), corners→(0,0) and (afW,afH), and clicks in the
  letterbox margin are rejected.
- Reticle state transitions: Pending on click → Ok/Failed on `setAfResult`.
- Manual/hardware: on the D750-family body, click center (AF lands center),
  then corners; adjust the calibration spin boxes until AF matches the reticle.

## Out of scope

- Parsing the Nikon live-view header / raw PTP live-view path.
- True focus-confirm (in-focus vs out-of-focus) feedback.
- Non-Nikon focus-point APIs (Canon/Sony).
- AF box size from the camera (uses a fixed display fraction).
