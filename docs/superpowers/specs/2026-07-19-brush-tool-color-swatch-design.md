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
- A raw per-pixel `QImage` paint buffer — paint strokes are stored as
  vector stroke points (like the existing Brush mask type), not literal
  bitmap pixels (see "Architecture note" below).

## Architecture note (supersedes initial "paint onto active layer" idea)

This codebase has no per-layer rasterized pixel buffer: layers (`Mask` in
`src/edit/Adjustments.h`) are procedural — geometry (radial/linear/brush
stroke) + tone adjustments, composited fresh on every render in
`applyMasks()` (`src/edit/Adjustments.cpp`). There is no bitmap layer type
and no `QPainter`-based compositing path to paint into directly.

The Brush tool is therefore implemented as a **new paint layer kind**,
sibling to the existing `MaskType::Brush` mask: it reuses the exact same
stroke storage (`QVector<BrushStrokePoint> stroke`, `brushRadius`,
`hardness`) and the same `rasterizeBrush()` coverage/opacity/blend
pipeline already in `applyMasks()`. The only new per-layer data is a
`QColor paintColor` field. During compositing, a paint layer's "content"
(the `loc` image in `applyMasks()`) is a flat fill of `paintColor` instead
of `applyLayerContent(img, m.adj)`. This makes paint layers real, visible,
reorderable, deletable entries in the Layers panel — consistent with every
other layer type — without adding a new pixel-buffer subsystem.

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

No new tool-logic files. The brush is implemented as a new **paint layer
kind**, sibling to `MaskType::Brush`, living entirely inside the existing
`Mask`/`applyMasks()` machinery in `src/edit/Adjustments.h/.cpp`, plus UI
wiring in `ImageCanvas`, `RetouchTab`, `RetouchWindow`, and `LayersPanel`.

### Data model changes (`src/edit/Adjustments.h`)

- Add `MaskType::Paint` to the `enum class MaskType`.
- Add `QColor paintColor = Qt::black;` to `Mask`, included in
  `Mask::operator==`.
- A `Mask` with `type == MaskType::Paint` reuses the existing `stroke`,
  `brushRadius`, and `hardness` fields exactly like `MaskType::Brush` (same
  stroke-point storage, same `rasterizeBrush()` coverage rasterization).
  `adj` (`MaskAdjust`) is unused for paint layers.

### Compositing changes (`src/edit/Adjustments.cpp`)

In `applyMasks()`, where `loc` (the layer's content image) is currently
computed as:
```cpp
const QImage loc = imageLayer
        ? applyLayerContent(coverFit(m.sourceImageCache, w, h), m.adj)
        : applyLayerContent(img, m.adj);
```
add a branch for paint layers that fills a flat `paintColor` image instead:
```cpp
const bool paintLayer = m.type == MaskType::Paint;
const QImage loc = imageLayer
        ? applyLayerContent(coverFit(m.sourceImageCache, w, h), m.adj)
        : paintLayer ? QImage(w, h, QImage::Format_ARGB32) /* filled below */
                      : applyLayerContent(img, m.adj);
```
(exact fill mechanics detailed in the implementation plan). Coverage
(`cov`) for `MaskType::Paint` is computed via the same
`rasterizeBrush(m, cov, w, h, &img)` call already used for
`MaskType::Brush` — extend that `if` condition to include `Paint`. Opacity
and `BlendMode` compositing in the pixel loop below are unchanged (already
generic over any `wgt`/`loc` source).

Also extend `hasMaskEdits()` and the `m.type == MaskType::Brush &&
m.stroke.isEmpty()) continue;` skip-check to treat `Paint` the same as
`Brush` (skip empty-stroke paint layers).

### Activation

A new "Brush" toolbar action is added next to the existing Heal action in
`RetouchWindow::setupUi`, using the same icon-drawing pattern
(`makeHealIcon` sibling, e.g. `makeBrushIcon`), added to the same manual
mutual-exclusion lambda chain as Heal/Mask/Crop/Zoom/WB-pick.
`ImageCanvas` gets `bool m_paintMode` + `setPaintMode(bool on)`, mirroring
`setHealMode`.

Selecting the Brush tool creates (or reuses, if already selected) a
`MaskType::Paint` layer in `RetouchTab`'s `m_adj.masks`, set as the active
mask, exactly as clicking the mask-brush subtool creates/selects a brush
mask today — so paint strokes land in the Layers panel as a normal,
visible, reorderable, deletable, opacity/blend-adjustable layer.

### Options bar

While Brush is active, an options row (mirroring the existing Spot Heal
options row in `RetouchWindow::buildToolOptionsBar()`) shows:
- **Size** — also adjustable via ctrl+wheel on canvas
  (`setBrushRadius`/`maskBrushRadiusChanged`, already shared with the mask
  brush).
- **Hardness** — 0–100%, maps directly to `Mask::hardness` (same field the
  mask brush already uses).
- **Opacity** — 0–100%, maps directly to `Mask::opacity` (already exists
  on every layer).

### Painting behavior

- Reuses `ImageCanvas`'s existing mouse-event plumbing and
  display→image-normalized coordinate mapping via the same
  `maskBrushPoint(QPointF ptNorm, bool erase)` signal the mask brush
  already emits (no new canvas signal needed) — the paint tool just routes
  through the same path when `m_paintMode` is active instead of
  `m_maskMode`.
- `RetouchTab::onMaskBrushPoint` (or a thin paint-mode equivalent) appends
  to the active paint layer's `stroke` list and calls `retone()`, exactly
  like the mask brush.
- On mouse release, `markEdited()` is called once (mirroring
  `onMaskEditFinished()`), so a full drag stroke coalesces into one
  history entry via the existing 350ms-debounced commit system — no new
  undo/history code needed.

### Color source

`RetouchTab` connects `ColorSwatchWidget::foregroundColorChanged` to set
`paintColor` on the active paint layer (and on newly-created paint layers
at creation time), so new strokes always use the current foreground color.
Changing fg color while an existing paint layer is selected does not
retroactively recolor strokes already drawn on a *different* paint layer —
each paint layer has its own fixed `paintColor` once created (matches how
each mask layer has its own fixed `adj`).

## Testing

- Manual verification in the running app (Qt Widgets desktop app — no
  automated UI test harness currently exists for Retouch tools, consistent
  with Heal/Mask tools which are also manually verified).
- Verify: swatch click opens color dialog and updates the square; swap
  arrow and `X` key flip fg/bg; reset icon and `D` key reset to
  black/white; brush creates a new Paint layer visible in the Layers
  panel; strokes render in the current fg color at correct
  size/hardness/opacity; ctrl+wheel resizes brush; a full drag stroke
  undoes as one step via Ctrl+Z; paint layer can be reordered, hidden,
  deleted, and have its opacity/blend mode changed like any other layer.
