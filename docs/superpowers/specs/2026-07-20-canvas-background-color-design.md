# Canvas Background Color — Design

## Context

An in-progress merge (`MERGE_HEAD` = "Improve image layer selection handles") left
`src/edit/ImageCanvas.cpp` with a botched conflict resolution: a `}` closing
`if (m_colorRangeDragging)` was dropped, which nests the image-layer selection-handle
gizmo inside that block. Separately, an earlier commit ("feat: add canvas background
context menu") added a `m_backgroundColor` member and a declared-but-undefined
`contextMenuEvent` override, but never implemented the actual feature.

## Goal

Finish the canvas background color feature: let the user right-click the canvas to
change its background color, with the choice persisted per-image in the edit sidecar.

## Scope

1. **Fix the merge conflict** — restore the missing `}` in `ImageCanvas::paintEvent`
   so the color-range-drag block and the image-layer selection-handle block are
   siblings, not nested. Stage the resolved file to complete the merge.

2. **Background color state** — `ImageCanvas` keeps `QColor m_backgroundColor`
   (default `QColor(30,30,30)`), gains `setBackgroundColor(const QColor&)` /
   `backgroundColor() const`, and a `backgroundColorChanged(QColor)` signal.
   `paintEvent` fills with `m_backgroundColor` instead of the hardcoded literal.

3. **Context menu** — implement `ImageCanvas::contextMenuEvent`. Menu entries:
   Black, White, Gray (`#808080`), separator, "Custom...", "Reset to Default".
   "Custom..." opens `QColorDialog::getColor` seeded with the current color.
   "Reset to Default" sets `QColor(30,30,30)`. Any selection calls
   `setBackgroundColor` and emits `backgroundColorChanged`.

4. **Persistence** — add a `backgroundColor` field to `EditSidecar` (stored as an
   RGB hex string), bump sidecar version 5 → 6. Missing/old sidecars default to
   `QColor(30,30,30)`. `RetouchTab`/`RetouchWindow` connect
   `backgroundColorChanged` to mark the sidecar dirty and save it, and restore it
   when an image is loaded, following the existing pattern used for the
   image-layer transform fields.

5. **Tests** — extend the sidecar round-trip test to cover background color
   save/load, and a case confirming old sidecars (version < 6) default correctly.

## Out of scope

- Global/app-level default color setting (QSettings) — per-image only, per user decision.
- Arbitrary preset list beyond Black/White/Gray — fixed three presets plus custom.
- Any change to the image-layer transform feature beyond restoring correct brace
  nesting in the merge conflict.

## Risks / notes

- Sidecar version bump must not break loading of existing sidecar files at
  version ≤ 5 (default background color assumed for those).
- `contextMenuEvent` must not swallow context-menu clicks that should hit other
  interactive elements (e.g. it should only trigger this menu when the canvas
  background itself — not an active layer/handle — is right-clicked, consistent
  with the original "canvas background context menu" commit's intent).
