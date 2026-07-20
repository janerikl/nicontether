# Photoshop-style Background Layer & File > New — Design

## Context

Layers today live in `QVector<Mask> masks` (`Adjustments.h:312`), shown and
reordered via `LayersPanel`. The opened photo itself is **not** an entry in
that stack — it's the implicit base `Adjustments` (brightness/contrast/tone/
color/curves/levels/heals), always edited directly through the always-visible
adjustment panels, with no row in `LayersPanel` at all
(`LayersPanel::rebuildList()` iterates only `m_masks`).

Every `RetouchTab` today requires a real file path: the constructor
immediately calls `EditSidecar::load(m_path, ...)` and kicks off
`QtConcurrent::run(RawLoader::load, m_path)`. `m_openTabs`
(`QMap<QString, RetouchTab*>` in `RetouchWindow.h`) is keyed by that path
everywhere (open, close, save-all, filmstrip). There is no "Save As" flow,
and no File > New action exists.

The erase tool already establishes the gating pattern this design reuses:
disabled unless the active layer `isImageLayer()` (`RetouchWindow.cpp:1359-1365`).

## Goal

1. When a photo is opened (File > Open or filmstrip), it appears as a locked
   "Background" row pinned to the bottom of the Layers panel. It can be
   duplicated into a real, unlocked image layer, but destructive tools
   (erase, heal/spot-removal) cannot be applied to it directly. Non-destructive
   adjustments (tone/color/curves/levels) continue to apply to it exactly as
   today — unchanged.
2. A new File > New action creates a blank canvas at a user-chosen size, as
   an "Untitled" tab. That base layer starts fully editable (no lock) since
   there's no original file to protect. On first save, the user is prompted
   for a destination; once saved, the tab behaves like any file-backed tab,
   including gaining the same locked Background treatment from then on.

## Scope

### 1. Locked Background row (opened photos)

- No new field on `Mask`/`Adjustments`. The base layer's data and its
  tone/color/curves/levels editing path are unchanged.
- `LayersPanel::rebuildList()` prepends a synthetic row (not backed by a
  `Mask`) whenever the current tab has a real file path: labeled
  "Background", lock icon, no drag handle, delete action disabled/hidden.
  The only enabled action on this row is **Duplicate**.
- Duplicate on the Background row creates a new `Mask` (image layer:
  `sourceImagePath` = the tab's photo path, identity offset/scale, no
  `eraseStrokes`/`heals`) inserted at index 0 of `masks` and selected as the
  active layer — a normal, fully unlocked image layer from that point on.
- "Active layer is Background" (no mask selected) is treated the same way
  `!isImageLayer()` is treated today: the erase-tool gating in
  `RetouchWindow.cpp:1359-1365` is extended to also disable when no mask is
  selected, and the same gating is added to the heal/spot-removal tool's
  enable check (new — heal currently has no such gating since it applies to
  `m_adj.heals` directly regardless of selection).
- Tone/color/curves/levels panels are untouched: they keep editing
  `m_curAdjust`/base exactly as today, regardless of Background lock state.

### 2. File > New

- New "New…" `QAction` in the File menu (`RetouchWindow.cpp:390-416`), opens
  a modal dialog: width, height, unit selector (px / in / cm), DPI field
  (used only for unit conversion to pixels).
- Creates an `RetouchTab` via a new constructor path that accepts no file
  path plus a target `QSize`. In `RetouchTab`'s ctor, when path is empty:
  skip `EditSidecar::load` and `RawLoader`/`QtConcurrent::run` entirely,
  synchronously initialize the base image to a transparent
  `QImage(size, Format_ARGB32)` filled `Qt::transparent`, and mark the tab
  ready immediately (no async decode state).
- `m_openTabs` keys this tab with a synthetic string id (`"untitled:1"`,
  `"untitled:2"`, ...) distinct from any real path, so multiple untitled
  tabs can coexist with path-keyed ones in the same `QMap<QString, ...>`.
- Tab title is `"Untitled-N"` (N = a simple incrementing counter) instead of
  `QFileInfo(path).fileName()` at the `addTab()` call site
  (`RetouchWindow.cpp:1520`).
- Untitled tabs are not added to the filmstrip and are not touched by
  session load/save (`loadSession()` only ever scans a folder for `.nef`
  files and calls `addToFilmstrip`/`openPhoto` — untitled tabs are simply
  never referenced by that path, matching how it already ignores anything
  not explicitly added).
- No Background row/lock applies to an untitled tab's base layer — it's a
  normal, fully editable layer, consistent with the fact that "locked"
  above is defined as "this tab has a real backing file."

### 3. First save of an untitled tab

- `onSave()`/`onSaveAll()` (`RetouchWindow.cpp:1916-1929`) gain a check: if
  the tab's path is empty, show a save-file dialog (new — no existing
  "Save As" flow to extend) to pick a destination image path.
- On confirmation: write the current base canvas to that path as an image
  file, set the tab's path, re-key it in `m_openTabs` from its synthetic id
  to the real path, and rename its tab label from `"Untitled-N"` to the
  chosen filename.
- From that point the tab is indistinguishable from any other file-backed
  tab: normal `saveEdits()`/sidecar path, and it now also gains the locked
  Background row (per rule in Scope §1 — locking is purely a function of
  "has a real path", so this falls out naturally rather than needing a
  special case).
- This reuses the existing `EditSidecar`/`saveEdits()` pipeline unchanged;
  no sidecar format version bump is needed anywhere in this design.

## Out of scope

- Full Photoshop-style New dialog (presets, color mode, bit depth,
  background-contents choice). Only width/height/unit/DPI.
- Adding the saved-from-blank-canvas tab to the filmstrip automatically
  (it becomes file-backed but the filmstrip is only populated by
  `openPhoto`/`loadSession` explicitly — out of scope to also wire it in
  here unless requested later).
- Opacity/blend controls on the Background row (it has none today and none
  are added — only tone/color/curves/levels, which already apply to it).
- Locking/gating any tool other than erase and heal (e.g. future paint
  tools would need the same treatment when added, not handled here).

## Risks / notes

- Heal tool currently has no active-layer gating at all (it always targets
  `m_adj.heals`); adding gating here is a small but real behavior change —
  worth confirming in manual testing that duplicating the Background layer
  and healing on the duplicate still works exactly like healing does today
  on a normal image layer.
- `m_openTabs`'s two full-map iterations (`onSaveAll`, and the one around
  line 1629) must tolerate synthetic-id entries with an empty `path()` —
  audit both loops when implementing to ensure they skip/prompt rather than
  assume a valid path.
- Re-keying a `QMap` entry (removing the synthetic-id key and inserting
  under the new real path) must happen atomically with the tab label
  rename to avoid a stale "Untitled-N" tab title after save.
