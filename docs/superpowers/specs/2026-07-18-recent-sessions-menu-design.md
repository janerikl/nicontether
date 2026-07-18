# Recent Sessions in File Menu — Design

**Date:** 2026-07-18
**Status:** Approved (design)

## Goal

Let the user reopen a recently opened session with a single click from the
File menu in the RAW editor (`RetouchWindow`), instead of navigating the
directory picker each time.

## Context

- A "session" in this app is a **folder of NEF files** — there is no session
  file format. Sessions are opened via **"Open Session…"**
  (`RetouchWindow::onOpenSession`), which is a directory picker that scans the
  chosen folder for `*.nef` and loads them into the filmstrip.
- The File menu is built in `src/edit/RetouchWindow.cpp` (~lines 113–120).
- No recent-files mechanism exists today.
- `QSettings` (org/app "NikonTether") is already used for export presets
  (`src/edit/ExportPreset.cpp`), and is the storage mechanism for this feature.

## Scope

- Track **sessions only** (folders opened via "Open Session…"). Individual
  "Open Photos…" selections are out of scope.
- Show up to **5** most-recent sessions.

## Behavior

- The File menu shows up to 5 most-recently-opened sessions **inline**, under a
  separator below "Open Photos…":

  ```
  Open Session…
  Open Photos…
  ──────────────
  <recent session 1>
  <recent session 2>
  ...
  ──────────────
  Save
  Save All
  ──────────────
  Export…
  ```

- Each recent entry displays the folder name; the full absolute path is shown
  as the item's tooltip.
- Clicking an entry reopens that session via the same loading path as
  `onOpenSession`, skipping the directory picker.
- Ordering: newest on top. Reopening an existing session (via picker or recent
  entry) moves it back to the top. Entries are de-duplicated by absolute path.
- **Missing folder:** when a clicked entry's folder no longer exists on disk,
  show a `QMessageBox` warning, remove that entry from the recent list, and
  rebuild the menu. (No load attempted.)
- **Empty list:** when there are no recent sessions, neither the recent items
  nor the surrounding separator are shown.

## Storage

- Persisted via `QSettings` (existing "NikonTether" org/app).
- New small store `RecentSessions` holds a `QStringList` of absolute folder
  paths under the key `recentSessions`, capped at 5, de-duplicated by path,
  newest first. Modeled on the array-based pattern in `ExportPreset.cpp`.

## Components

### New: `src/edit/RecentSessions.h` / `.cpp`

A small, UI-independent store. Pure list logic; only dependency is `QSettings`.

- `static QStringList load();` — read the capped list from QSettings.
- `static void add(const QString &absPath);` — insert at front, de-dup by
  path, cap to 5, persist.
- `static void remove(const QString &absPath);` — drop an entry, persist.
- Internal constant `kMaxRecent = 5`.

### Changed: `src/edit/RetouchWindow.cpp` / `.h`

1. **Extract** the folder-loading body of `onOpenSession()` into a private
   `void loadSession(const QString &dir);`. `onOpenSession()` picks a directory
   then delegates to `loadSession`. On successful load, `loadSession` calls
   `RecentSessions::add(dir)` and rebuilds the recent menu section.
2. **Add** `void rebuildRecentSessionsMenu();` — clears and repopulates the
   recent portion of the File menu (items + surrounding separator), reflecting
   the current `RecentSessions::load()`. Called at construction and after each
   open / removal.
3. **Recent-entry click handler** — checks `QDir(dir).exists()`; if missing,
   warn via `QMessageBox`, call `RecentSessions::remove(dir)`, rebuild, and
   return; otherwise call `loadSession(dir)`.
4. **File menu construction** — retain a handle to the File menu (and the
   anchor points for the recent section) so it can be rebuilt; call
   `rebuildRecentSessionsMenu()` once during construction.

### Changed: `CMakeLists.txt`

Add `src/edit/RecentSessions.cpp` to the build.

## Error Handling

- Missing session folder on click: warn + remove + rebuild (above).
- `loadSession` reuses existing NEF-scan behavior; if a folder exists but
  contains no NEFs, behavior is unchanged from today's `onOpenSession`.

## Testing

- `RecentSessions` add/dedup/cap/remove is pure list logic over QSettings and
  is unit-testable using an isolated QSettings scope (e.g. a temp
  org/app or `QSettings::setPath` in the test).
- Menu rebuild and missing-folder warning verified manually by opening several
  sessions and deleting one folder.

## Out of Scope

- Session file format / true session documents.
- Tracking "Open Photos…" selections.
- A "Clear recent" action (can be added later if desired).
