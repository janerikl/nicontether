# Crop Drag Handles + Esc-Resets-Tool — Design

Date: 2026-07-18

## Summary

Two related improvements to the RAW retouch window (`src/edit/`):

1. **Crop drag handles** — add 8 resize handles (4 corners + 4 edges) to the crop
   rectangle in `ImageCanvas`, so an existing crop can be resized by grabbing a
   corner or edge. Handles render as corner brackets (L-shapes) plus edge ticks,
   with rule-of-thirds gridlines shown while dragging. Active aspect-ratio
   constraint is respected during resize.
2. **Esc resets the active tool** — pressing Escape anywhere in the retouch
   window deselects the currently active tool (zoom, crop, spot-heal, wb-pick),
   returning to the idle/no-tool state.

## Motivation

Today the crop tool only supports **create** (drag from scratch) and **move**
(drag whole box). There is no way to grab an edge/corner to resize — the user
must redraw. Adding handles brings it in line with Lightroom/Photoshop crop UX.
Separately, there is no quick keyboard way to back out of an active tool; Esc is
the conventional key for this.

## Part 1 — Crop Drag Handles

All changes are in `src/edit/ImageCanvas.h` and `src/edit/ImageCanvas.cpp`.

### State

Extend the interaction model:

- Extend `enum class Drag` from `{ None, Creating, Moving }` to add `Resizing`.
- Add `enum class Handle { None, TopLeft, Top, TopRight, Right, BottomRight,
  Bottom, BottomLeft, Left }`.
- Add members: `Handle m_activeHandle = Handle::None;` and
  `QRect m_rectAtDragStart;` (the selection rect captured at press, in widget
  coords).

### Hit-testing (`mousePressEvent`)

On left-press in crop mode, test in this precedence order:

1. **Handle** — if the cursor is within a grab tolerance (~10 px) of one of the
   8 handle zones of `selectionRect()`, start `Drag::Resizing` with that
   `Handle`, and record `m_rectAtDragStart`.
2. **Inside** — else if inside `selectionRect()`, start `Drag::Moving` (existing
   behavior).
3. **Outside** — else start `Drag::Creating` (existing behavior).

A helper `Handle handleAt(const QPoint&) const` performs the hit-test against the
current `selectionRect()`.

### Resize (`mouseMoveEvent`)

For `Drag::Resizing`, move the edge(s) associated with `m_activeHandle` to follow
the cursor, starting from `m_rectAtDragStart`:

- Corner handles move two edges (the two meeting at that corner).
- Edge handles move one edge.

Then:

- Clamp the resulting rect to `targetRect()` (image bounds).
- If an aspect ratio is active (`m_cropAspect != 0`), preserve the ratio by
  routing the moved corner through the existing `constrainedCorner()` logic,
  anchoring the opposite corner (for corner handles) or deriving the free
  dimension from the constrained one (for edge handles).
- Rewrite `m_p0`/`m_p1` from the resulting rect (same as move does today), then
  `update()`.

### Cursor feedback

When idle in crop mode, hover over a handle sets a directional cursor:

- TopLeft / BottomRight → `Qt::SizeFDiagCursor`
- TopRight / BottomLeft → `Qt::SizeBDiagCursor`
- Top / Bottom → `Qt::SizeVerCursor`
- Left / Right → `Qt::SizeHorCursor`
- Inside selection → `Qt::SizeAllCursor` (existing)
- Otherwise → `Qt::CrossCursor` (existing)

### Rendering (`paintEvent`)

After the existing darken-overlay + white dashed rectangle, and only when the
selection is non-empty:

- **Corner brackets** — L-shaped white lines (~2 px wide, ~18 px legs) inset just
  inside each of the 4 corners.
- **Edge handles** — a short white bracket/tick centered on each of the 4 edge
  midpoints.
- **Rule-of-thirds gridlines** — 2 vertical + 2 horizontal thin, semi-transparent
  white lines dividing the crop into thirds. Drawn **only while
  `m_drag != Drag::None`** (actively creating/moving/resizing); hidden on release.

### Unchanged

`selectionRect()`, `selectionInImage()`, the `cropSelected()` /
`commitCropRequested()` signals, and the `RetouchTab` commit path are unchanged.
Resizing only mutates `m_p0`/`m_p1` exactly like move; release still emits
`cropSelected(selectionInImage())`.

## Part 2 — Esc Resets Active Tool

All changes are in `src/edit/RetouchWindow.h` and `src/edit/RetouchWindow.cpp`.

- Extract the existing tool-deselect block in `onTabChanged()` (currently around
  lines 591–610: zoom, crop, wb-pick, heal — each `QSignalBlocker` +
  corresponding `tab->set…Mode(false)`) into a new private method
  `void deselectAllTools();`. Call it from `onTabChanged()` in place of the
  inlined block to avoid duplication.
- Add a `QShortcut` for `Qt::Key_Escape` on the `RetouchWindow` (WindowShortcut
  context) whose slot calls `deselectAllTools()`.

### Behavior notes

- Esc deselects whichever tool is active and returns to the idle/no-tool state.
- Esc fully exits crop mode, discarding any un-applied selection (consistent with
  "reset the tool"). Enter still commits the crop as it does today.

## Testing

Manual verification in the running app (Qt Widgets; no unit-test harness for the
canvas interaction):

1. Draw a crop, then grab each of the 8 handles and confirm resize in the
   expected direction; confirm cursor changes on hover.
2. With an aspect ratio active (e.g. 3:2), confirm handle resize preserves the
   ratio; with Freeform, confirm independent edge movement.
3. Confirm rule-of-thirds gridlines appear only while dragging and disappear on
   release.
4. Confirm handles/brackets render correctly and stay within the crop box.
5. With each tool active (zoom, crop, heal, wb-pick), press Esc and confirm the
   tool button un-checks and the window returns to idle.
6. Confirm Enter still commits a crop and Esc discards an un-applied one.

## Out of Scope

- No changes to crop aspect presets, the toolbar, or the sidecar/serialization.
- No rule-of-thirds display outside of active dragging.
- No rotation handle.
