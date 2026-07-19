# Grid / Composition Overlays for Tether Live View

**Date:** 2026-07-19
**Status:** Approved

## Goal

Let the user overlay composition guides on the tether live view while shooting.
The user right-clicks the live view canvas and picks one guide from a menu. The
choice is remembered across app launches.

## Overlays

Radio-style — exactly one active at a time (or none):

1. **Off**
2. **Rule of thirds** — 3×3 grid at 1/3 and 2/3.
3. **Golden ratio (phi grid)** — vertical/horizontal divisions at ~0.382 and ~0.618.
4. **Golden spiral** — Fibonacci spiral following the phi grid.
5. **Center crosshair** — one vertical + one horizontal line through the center.
6. **Diagonals** — corner-to-corner diagonals plus harmonious-diagonal lines.

## Architecture

Follows the existing `LiveViewWidget` patterns (matches `m_calibrating` /
`setCalibrationMode`).

- Add `enum class GridMode { Off, Thirds, GoldenRatio, GoldenSpiral, Crosshair, Diagonals }`.
- Add member `GridMode m_gridMode = GridMode::Off`.
- Add `void setGridMode(GridMode)` — stores the value, persists it, calls `update()`.
- Add `GridMode gridMode() const`.

### Rendering

- Draw in `paintEvent`, **after** `drawImage` and **before** the AF reticle /
  calibration crosshair (so focus indicators stay visually on top).
- Clip / compute all geometry within `drawnRect()` (the aspect-preserving,
  centered, letterboxed image rectangle) so lines fall on the image, never on the
  black letterbox bars.
- Private helper `void drawGrid(QPainter&, const QRect&)` switching on `m_gridMode`.
- Line style: semi-transparent white (~70% opacity, 1px) with a subtle darker
  under-stroke for contrast on light images. No image assets.

### Selection UI

- Override `contextMenuEvent(QContextMenuEvent*)` on `LiveViewWidget`.
- Build a `QMenu` with a `QActionGroup` (exclusive) of 6 checkable actions.
- The action matching `m_gridMode` is checked.
- Triggering an action calls `setGridMode(...)`.
- Left-click tap-to-focus (`mousePressEvent`) is unchanged.

### Persistence

- `setGridMode` writes the mode to `QSettings` under key `liveview/gridMode`
  (stored as int).
- `LiveViewWidget` constructor reads `liveview/gridMode` and restores it.

## Testing

Qt `QImage`-render tests (match project test setup if one exists; otherwise a
minimal standalone test):

- For each non-Off mode: render to an offscreen `QImage`, assert overlay pixels
  changed **within** `drawnRect()` and **not outside** it.
- `QSettings` round-trip: set a mode, construct a fresh widget, assert restored.

## Out of Scope (YAGNI)

- Multiple simultaneous overlays.
- User-configurable colors/opacity/line weight.
- Custom/user-defined grids.
