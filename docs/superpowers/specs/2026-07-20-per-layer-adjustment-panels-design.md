# Per-Layer Adjustment Panels

**Date:** 2026-07-20
**Status:** Approved design

## Goal

Split the per-layer adjustment sections currently built inline inside
`LayersPanel` (Tone, Colour, Tone Curve, Detail & Effects) into standalone
dockable `QDockWidget` panels — one per section — matching how `LevelsPanel`
and `MaskPanel` already work today. By default (and after "Reset Panels")
these six panels — Tone, Color, Tone Curve, Levels, Detail & Effects, Masks —
tabify together with the Layers dock, but each can be dragged out, floated,
resized, or hidden independently like any other dock.

This is **not** about the global, image-wide Adjustments dock
(`buildDock()`, `RetouchWindow.cpp:832-960`) or its Orientation section —
those are untouched.

## Context

- `LayersPanel` (`src/ui/LayersPanel.h/.cpp`) currently builds, inline in its
  constructor (`LayersPanel.cpp:58-133`): the layer list, Name/Opacity/Blend
  row, a Tone section (Brightness/Contrast/Highlights/Shadows), a Colour
  section (Saturation/Vibrance/Temperature/Tint), an embedded `CurveEditor`
  (Tone Curve), an embedded `LevelsPanel` (Levels), and a Detail & Effects
  section (Clarity/Sharpen/Vignette).
- `LayersPanel.h:18-25` documents this as "the complete tone/colour/curve/
  levels/detail adjustment set — the same power as the main Adjustments dock,
  scoped to this layer."
- Per-layer adjustment state lives in `MaskAdjust` (`src/edit/Adjustments.h:57`),
  one instance per `Mask` (`Adjustments.h:131`, the per-layer struct — there
  is no separate Paint/Adjustment layer class).
- `LayersPanel::loadActive()` (`LayersPanel.cpp:225-260`) pushes the selected
  layer's `MaskAdjust` into all the inline controls; `emitAdjust()`
  (`:262-286`) pushes edits back out via a `maskAdjustChanged(const
  MaskAdjust&)` signal.
- `MaskPanel` (`src/ui/MaskPanel.h/.cpp`) is already a separate `QWidget`,
  hosted in its own `m_maskDock`, built by `RetouchWindow::buildLayersDock()`
  (`RetouchWindow.cpp:1034-1123`) and tabified with `m_layersDock`. Unlike the
  new panels, it's shown/hidden based on active tool — that stays as-is.
- `RetouchWindow::refreshMaskPanel()` (`RetouchWindow.cpp:1125`) is the
  existing sync point that pushes the active layer's state into docks when
  selection changes.
- Default dock arrangement and panel persistence follow the pattern already
  established in `applyDefaultDockLayout()` (`RetouchWindow.cpp:486-511`) and
  the object-name / `QSettings` save/restore scheme from the
  [[2026-07-19-persistent-panel-layout-design]] spec — reuse both directly,
  don't reinvent.

## Design

### 1. Extract four new panel widget classes

Add to `src/ui/`, each a plain `QWidget` subclass mirroring `LevelsPanel`'s
shape (`setAdjustments(const MaskAdjust&)`, `clear()`, an `adjustChanged(...)`
signal carrying just the fields it owns):

- `TonePanel` — Brightness/Contrast/Highlights/Shadows sliders, moved verbatim
  from `LayersPanel.cpp:86-99`.
- `ColorPanel` — Saturation/Vibrance/Temperature/Tint sliders, moved from
  `LayersPanel.cpp:101-107`.
- `ToneCurvePanel` — thin wrapper hosting the existing `CurveEditor` widget
  (no changes to `CurveEditor` itself), moved from `LayersPanel.cpp:109-113`.
- `DetailEffectsPanel` — Clarity/Sharpen/Vignette sliders, moved from
  `LayersPanel.cpp:124-129`.

`LevelsPanel` and `MaskPanel` are reused unchanged.

### 2. Shrink LayersPanel

`LayersPanel` keeps only the layer list and the Name/Opacity/Blend row. It
keeps its existing layer-selection signal. All adjustment-section code is
deleted from `LayersPanel.cpp` once moved into the new classes.

### 3. New docks in RetouchWindow

Add `buildTonePanelDock()`, `buildColorPanelDock()`, `buildToneCurveDock()`,
`buildDetailEffectsDock()` — each following `buildLevelsDock()`
(`RetouchWindow.cpp:997-1011`) as the template: construct the panel, wrap in a
`QDockWidget`, `setObjectName(...)` (per the persistence spec), add via
`addDockWidget(Qt::RightDockWidgetArea, ...)`, store the `QDockWidget*` and
panel pointer as new `RetouchWindow` members.

### 4. Central sync point

Generalize `refreshMaskPanel()` into a method that, on active-layer change,
pushes the selected layer's `MaskAdjust` into **all six** panels — Tone,
Color, Tone Curve, Levels, Detail & Effects, Masks — regardless of each
panel's current dock/float state, per your confirmation that panels always
track the selected layer. Each panel's `adjustChanged` signal routes through
the same path `LayersPanel::maskAdjustChanged` used before, updating the
active `RetouchTab`.

### 5. Visibility

All six panels are visible whenever a layer is selected — unlike `MaskPanel`
today, which additionally hides unless a mask/brush tool is active. That
tool-driven hide/show logic for `MaskPanel` is unchanged; the five new/reused
panels (Tone, Color, Tone Curve, Levels, Detail & Effects) have no such
condition.

### 6. Default layout

Update `applyDefaultDockLayout()` (`RetouchWindow.cpp:486-511`) to tabify all
six docks — Tone, Color, Tone Curve, Levels, Detail & Effects, Masks — with
`m_layersDock`, replacing the current narrower tabify calls that only cover
Masks/Levels. Add a `toggleViewAction()` entry per new dock to the View menu
(`RetouchWindow.cpp:457-461`) so any of them can be reopened after being
closed.

### 7. Persistence

New docks get stable `setObjectName()`s and are covered automatically by the
existing `saveState()`/`restoreState()` machinery from the persistent-panel-
layout feature — no new persistence code needed beyond naming them.

## Non-goals

- No changes to the global/main Adjustments dock or Orientation controls.
- No new adjustment logic or fields — this is a UI reorganization of existing
  controls and existing `MaskAdjust` data.
- No change to `MaskPanel`'s tool-driven visibility behavior.
- No per-panel independent "reset to default position" — reset remains
  all-or-nothing via the existing "Reset Panels" action.

## Testing

- Select different layers in the layer list → Tone, Color, Tone Curve,
  Levels, Detail & Effects, and Masks panels all update to that layer's
  values, whether docked or floating.
- Edit a slider in a floated (undocked) panel → change applies to the active
  layer same as before extraction.
- View → Reset Panels → all six panels return tabified with Layers.
- Drag one panel (e.g. Tone) out to float independently, close the app,
  relaunch → its floated position/size is restored (via existing persistence).
- No layer selected → all six panels show empty/disabled state (matches
  current `clear()` behavior in `LevelsPanel`/inline sections).
