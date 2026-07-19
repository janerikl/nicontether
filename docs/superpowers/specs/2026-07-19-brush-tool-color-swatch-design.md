# Brush Tool + Foreground/Background Color Swatch

## Purpose

Add a Photoshop-style paintbrush to the Retouch tab, plus a flippable
foreground/background color swatch widget that supplies the paint color.
Today Retouch only has heal/spot-removal and mask (radial/graduated/brush)
tools — no way to paint arbitrary color onto a layer, and no color picker UI
exists anywhere in the app (the only "color" interaction is the white-balance
eyedropper in `RetouchTab::onColorPicked`).

## Scope

In scope:
- A custom fg/bg color swatch widget in the main Retouch toolbar.
- A new Brush tool that paints the current foreground color onto the active
  layer, with size, hardness, and opacity controls.

Out of scope (explicitly deferred):
- Clone stamp tool.
- Custom HSV color picker UI (uses Qt's built-in `QColorDialog`).
- Blend modes other than Normal (source-over) compositing.
- Painting onto a dedicated/new layer — brush always paints the currently
  active layer.

## 1. Color Swatch Widget

New files: `src/ui/ColorSwatchWidget.h` / `.cpp`.

A small (~32x32) custom `QWidget` added to the main toolbar in
`RetouchWindow::setupUi` via `toolbar->addWidget(...)`, visually consistent
with the existing hand-drawn toolbar icons (see `makeHealIcon` and friends).

Layout, modeled on Photoshop's swatch control:
- **Foreground square** — front/top-left square, filled with the current fg
  color. Click opens `QColorDialog` to choose a new fg color.
- **Background square** — back/bottom-right square, filled with the current
  bg color. Click opens `QColorDialog` to choose a new bg color.
- **Swap arrow** — small curved arrow icon at the top-right corner. Click
  flips fg ↔ bg. Also bound to the `X` key while the retouch canvas has
  focus.
- **Reset-to-default icon** — small black/white icon at the bottom-left
  corner. Click resets fg=black, bg=white. Also bound to the `D` key while
  the retouch canvas has focus.

Defaults: foreground = black, background = white.

Signals emitted:
- `foregroundColorChanged(QColor)`
- `backgroundColorChanged(QColor)`

State (fg/bg `QColor`) lives inside `ColorSwatchWidget`; it is the single
source of truth for current fg/bg color. No persistence across sessions is
required (resets to black/white on app restart).

## 2. Brush Tool

New files: `src/edit/PaintTool.h` / `.cpp` (mirrors the shape of
`HealTool.cpp`), plus wiring in `ImageCanvas`, `RetouchTab`, and
`RetouchWindow`.

### Activation

A new "Brush" toolbar action is added next to the existing Heal action in
`RetouchWindow::setupUi`, using the same icon-drawing/QAction pattern
(`makeHealIcon` sibling, e.g. `makeBrushIcon`). Selecting it switches
`ImageCanvas` into brush mode, mutually exclusive with Heal/Mask/Crop modes
(same selection-group pattern already used for those tools).

### Options bar

While Brush is active, a small options bar appears (mirroring `MaskPanel`'s
hardness/feather sliders) with:
- **Size** — also adjustable via ctrl+wheel on canvas, matching the existing
  heal/mask brush convention (`setBrushRadius` / `healBrushRadiusChanged`).
- **Hardness** — 0–100%, controls edge falloff of the brush stamp (soft
  radial gradient at low hardness, hard circular edge at 100%). Same concept
  as `MaskPanel`'s hardness control.
- **Opacity** — 0–100%, controls per-stroke alpha blending onto the layer.

### Painting behavior

- Reuses `ImageCanvas`'s existing mouse event plumbing
  (`mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent`) and
  display→image-normalized coordinate mapping — the same infrastructure the
  heal and mask brushes already use. Adds a new point-stream signal, e.g.
  `paintBrushPoint(QPointF ptNorm)`, alongside the existing
  `maskBrushPoint`/heal equivalents.
- `RetouchTab` connects `paintBrushPoint` to `PaintTool`, which rasterizes a
  brush stamp (radius = size, falloff = hardness) at each point and
  composites it in Normal (source-over) blend mode, at the given opacity,
  directly onto the pixel data of the currently active layer (per the
  existing Layers panel selection).
- Strokes are treated as a single undoable edit per mouse-down/up, going
  through the same undo/history stack as heal edits.

### Color source

`RetouchTab` connects `ColorSwatchWidget::foregroundColorChanged` to
`PaintTool`, so every new stroke paints with whatever the current
foreground color is at stroke-start time.

## Testing

- Manual verification in the running app (Qt Widgets desktop app — no
  automated UI test harness currently exists for Retouch tools, consistent
  with Heal/Mask tools which are also manually verified).
- Verify: swatch click opens color dialog and updates the square; swap
  arrow and `X` key flip fg/bg; reset icon and `D` key reset to
  black/white; brush paints fg color onto the active layer at correct
  size/hardness/opacity; ctrl+wheel resizes brush; stroke undoes as one
  step via Ctrl+Z.
