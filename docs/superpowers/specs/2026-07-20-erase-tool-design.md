# Erase Tool — Design

## Context

Image layers (`Mask` entries in `Adjustments.h` with `isImageLayer()==true`)
already composite through a `Format_ARGB32` buffer filled `Qt::transparent`
before the layer image is drawn, so per-pixel transparency is already
supported end-to-end. The app has no way to erase part of an image layer to
reveal transparency/the layer(s) beneath.

Existing tools establish two relevant patterns to follow:
- **Heal tool** (`src/edit/HealTool.h/.cpp`): stores op parameters
  (`x, y, radius`) in `Adjustments::heals`, replayed on every re-render
  (`applyHeal`, called from `RetouchTab.cpp:249`). No direct pixel mutation
  of a persisted buffer.
- **Mask brush**: stores `BrushStrokePoint` entries with an `erase` bool
  flag (`Adjustments.h:132-139`), appended via
  `RetouchTab::onMaskBrushPoint()`.

## Goal

Add an "Erase" tool: a brush that, when dragged over the currently selected
image layer, makes the erased region transparent with a soft/feathered
edge. Erasing is undo/redo-able via the existing history mechanism, but not
otherwise non-destructively editable (no separate always-visible mask
layer).

## Scope

1. **Data model** — add `sourceImageErases` to the `Mask` struct
   (`Adjustments.h`), a `QVector<ErasePoint>` where `ErasePoint` holds a
   normalized position (within the layer's local/source image space) and a
   normalized radius, mirroring `BrushStrokePoint`'s shape. Erase strokes
   are scoped per-layer (unlike `heals`, which is global on `m_adj`),
   because only the selected image layer should be affected.

2. **Compositing** — in `Adjustments.cpp`, after the fitted layer image is
   drawn into the layer's local `QImage` buffer (~line 390-424), replay
   `sourceImageErases`: for each point, paint a `QRadialGradient` (opaque
   center fading to transparent at the radius) using
   `QPainter::CompositionMode_DestinationOut`. This punches feathered
   transparency into that layer's alpha channel every time it's
   recomposited, consistent with how heal ops are replayed.

3. **Canvas input** — `ImageCanvas` gains `setEraseMode(bool)` (mirroring
   `setHealMode`) and, while active and the left mouse button is held over
   the canvas, emits an `eraseAt(QPointF ptNorm)`-style signal per sampled
   drag point (same sampling cadence as heal/mask brush dabs). Erase mode
   is only enabled when the currently selected layer is an image layer;
   otherwise the tool is disabled (toolbar button not checkable / no-op).

4. **Brush radius** — reuse the existing shared brush-radius state/control
   already used by the mask brush (`m_brushRadius`, `setBrushRadius`,
   `maskBrushRadiusChanged`) rather than adding a second radius control.

5. **Cursor preview** — reuse the existing brush-circle drawing code in
   `ImageCanvas::paintEvent()` (same mechanism as the heal/mask brush
   circles) to show a live circle at the current radius while the erase
   tool is active.

6. **RetouchTab wiring** — `RetouchTab::setEraseMode(bool)` forwards to
   `ImageCanvas::setEraseMode(bool)`. `RetouchTab::onEraseAt(QPointF)`
   appends an `ErasePoint` to the selected layer's `sourceImageErases`,
   calls `rebuildGeom()`, then `markEdited()` — following the exact
   `onHealAt` pattern (`RetouchTab.cpp:466-479`), so strokes coalesce into
   undo/redo history via the existing snapshot timer.

7. **Toolbar UI** — add an "Erase" `QToolButton` in `RetouchWindow.cpp`
   next to the Mask button, in the existing `QActionGroup` (mutually
   exclusive with other tools), icon via the existing `makeToolIcon`
   helper, shortcut key `E` (unused). Wire `toggled` to disable other tools
   and call `tab->setEraseMode(on)`, following the Heal button's wiring
   (`RetouchWindow.cpp:747-764`).

## Out of scope

- Non-destructive/repaintable erase masks that persist independent of
  pixel history (rejected in favor of the simpler replay-op + undo-stack
  model, consistent with heal/mask brush).
- Hard-edged (non-feathered) brush mode/toggle.
- Erasing on non-image layers (adjustment layers, background).
- New brush-radius UI — reuses the existing shared control.

## Risks / notes

- `sourceImageErases` must be included in sidecar serialization (mirroring
  however `heals`/mask brush strokes are currently persisted) so erasing
  survives reload — follow whatever existing pattern serializes `heals`.
- Because erase strokes replay in the layer's local coordinate space, the
  erase positions must be captured/converted consistently with how the
  layer's `sourceImageOffset`/`sourceImageScale` map canvas coordinates to
  layer-local coordinates, so erased regions stay aligned if the layer is
  later moved or resized.
