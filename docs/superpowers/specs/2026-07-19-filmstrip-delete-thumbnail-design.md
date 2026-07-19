# Filmstrip Thumbnail Delete — Design

Date: 2026-07-19

## Goal

Add a right-click "Delete" action to filmstrip thumbnails that moves the
underlying capture file(s) to the OS trash and removes them from the running
application.

## Decisions

- **Delete scope:** Move files to the OS trash (recoverable) via
  `QFile::moveToTrash()`. Not a filmstrip-only removal, not a permanent unlink.
- **Confirmation:** None. Delete happens immediately on menu-item click.
- **Multi-select:** The filmstrip uses `ExtendedSelection`. Conventional
  file-manager rule applies:
  - If the right-clicked item is part of the current selection → delete the
    whole selection.
  - If the right-clicked item is outside the selection → delete only that one.
- **Menu label:** Reflects the count — "Delete Photo" for one, "Delete N Photos"
  for many.

## Components & Changes

### FilmstripWidget (`src/ui/FilmstripWidget.{h,cpp}`)

- In the existing `contextMenuEvent(QContextMenuEvent*)` (currently building the
  "Open in Retouch" / "Sync Edits to Selected" menu), add a "Delete" action.
- Determine the target path list:
  - Read the clicked item's `Qt::UserRole` path.
  - If that path is among `selectedPaths()`, target = all selected paths.
  - Otherwise target = just the clicked path.
- Label the action based on target count.
- Add a new signal `void deleteRequested(const QStringList& paths);`.
- On action trigger, `emit deleteRequested(targetPaths)`.

This follows the exact pattern already used for `retouchRequested` — no
`setContextMenuPolicy`/`customContextMenuRequested`; the override builds a stack
`QMenu`, `exec()`s it, and compares the returned `QAction*`.

### RetouchWindow (`src/edit/RetouchWindow.{h,cpp}`)

Owns the runtime capture state (`m_filmstripPaths`, `m_openTabs`). Connect to
`FilmstripWidget::deleteRequested` with a new slot. For each path:

1. Move file to trash: `QFile::moveToTrash(path)`. Also trash the edit sidecar
   if it exists (`EditSidecar::pathFor(path)` when `EditSidecar::exists`).
2. If `moveToTrash` fails (returns false), **skip** UI/state removal for that
   path and record it for a status message — do not drop the thumbnail while the
   file remains on disk.
3. On success:
   - Remove the matching `QListWidgetItem` (`takeItem` + delete). Matched by
     comparing each item's `Qt::UserRole` string.
   - Erase the path from `m_filmstripPaths`.
   - If an editor tab is open for the path (`m_openTabs`), close and
     `deleteLater()` it, reusing the `onTabCloseRequested` teardown.

### Error handling

If any path failed to trash, show a brief non-modal status message (e.g. status
bar) naming how many could not be deleted. Successful paths are still removed.

## Testing

- The codebase has a `tests/` dir (CMake/QtTest, e.g. `AfCalibratorTest`).
- The trash call and Qt widget teardown are not cleanly unit-testable. Keep the
  path-selection logic (clicked-in-selection vs. clicked-alone) in a small pure
  helper so it can be unit-tested without a live widget, if a clean seam exists;
  otherwise verify manually.
- Manual verification: right-click single thumbnail → deletes it and its file;
  select several, right-click one of them → all deleted; right-click an
  unselected thumbnail while others are selected → only the clicked one deleted;
  deleting a thumbnail whose editor tab is open closes the tab.

## Out of Scope

- Undo within the app (recovery is via the OS trash).
- Keyboard Delete-key shortcut (could be a follow-up).
- Permanent-delete option.
