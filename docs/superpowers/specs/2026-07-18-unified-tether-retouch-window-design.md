# Unified Tether + Retouch Window — Design

**Date:** 2026-07-18
**Status:** Approved for planning

## Goal

Collapse the application's two separate top-level windows into a single window.
Today `MainWindow` (tethering / live capture) is the primary window and
`RetouchWindow` (photo editor) is a secondary window opened on demand. After this
change there is **one** application window: `RetouchWindow`, with tethering
reachable from its top navigation as a mode.

## Decisions (locked)

- **One window:** `RetouchWindow` becomes the sole top-level window. `MainWindow`
  is retired (class + files removed).
- **Interaction model:** Tether and Retouch are two full-view **modes** of the
  single window (not tabs, not docks). A central `QStackedWidget` swaps between
  them.
- **Startup mode:** Retouch.
- **Mode switch control:** two mutually-exclusive buttons at the left of the Main
  toolbar — **Tether** | **Retouch** — always visible.
- **Capture flow:** capturing stays in Tether mode and appends the new photo to
  the shared filmstrip. **Clicking any filmstrip thumbnail switches to Retouch
  mode with that photo open.**

## Architecture

### New component: `TetherView`

Extract all tether UI and camera ownership out of `MainWindow` into a new
self-contained `QWidget` subclass.

- **Files:** `src/ui/TetherView.h`, `src/ui/TetherView.cpp`
- **Owns:** `CameraController` (+ its `CameraWorker`), `SessionManager`, the
  `LiveViewWidget`, the `ControlsPanel`, the `PreviewWindow` (embedded as today,
  in its internal `QTabWidget` of Live View / Preview), and the tether actions
  (Connect / Disconnect / Live View / Capture / New Session).
- **What it does:** camera connect/disconnect, live view toggle, capture,
  session management — the current tether behavior, unchanged.
- **How it's used:** embedded as page 1 of the window's central stack. The host
  window supplies/holds the tether toolbar actions and the Controls dock.
- **Interface (signals up to the window):**
  - `captureComplete(const QString &path)` — a new photo was captured.
  - `errorOccurred(const QString &message)` — surface in the status bar.
  - `connectionChanged(bool connected)` — drive action enable/disable.
- **Dependencies:** `src/camera/*`, `src/capture/*`, existing tether widgets.
  No dependency on editing code.

`ControlsPanel` and the tether `QAction`s live in the Controls dock / toolbar
which the window creates; `TetherView` exposes the actions (or the window creates
them and connects to `TetherView` slots — planner picks the cleaner split, but the
Controls dock and tether toolbar are host-window chrome so they can be shown/hidden
per mode).

### `RetouchWindow` changes

- **Central widget becomes a `QStackedWidget`** with two pages:
  - Page 0 — **Retouch**: the existing per-photo `QTabWidget` (`m_tabs`).
  - Page 1 — **Tether**: the `TetherView`.
- **Mode buttons:** two checkable, mutually-exclusive `QAction`s/`QToolButton`s
  (`Tether`, `Retouch`) added at the left of the existing Main toolbar. They set
  the stack's current page and update contextual chrome.
- **Contextual chrome per mode** (`applyMode(Mode)` helper):
  - *Tether:* show tether toolbar (Connect/Disconnect/Live View/Capture/New
    Session) + Controls dock; hide editing Tools bar, Tool Options bar,
    Adjustments dock, History dock.
  - *Retouch:* reverse — show editing bars/docks, hide tether toolbar + Controls
    dock.
  - Save / Save All / Export toolbar actions and File/Edit menus remain; actions
    that only make sense while editing are disabled in Tether mode.
- **Shared filmstrip:** use `RetouchWindow`'s existing filmstrip, visible in both
  modes. Connect `TetherView::captureComplete` → append to filmstrip (replacing
  `MainWindow::handleCaptureComplete`'s `addToFilmstrip` path).
- **Capture-to-edit wiring:** the filmstrip's existing `retouchRequested`/open
  logic now (a) opens the photo in a Retouch tab and (b) switches the window to
  Retouch mode — reusing existing `openInRetouch`-style logic, but in-window
  instead of spawning a second window.
- **Session capture list** (`m_captures`) moves into `RetouchWindow` (or is owned
  by `TetherView` and read by the window).
- **Spacebar capture shortcut** is active in Tether mode.

### Retire `MainWindow`

- Delete `src/ui/MainWindow.h` and `src/ui/MainWindow.cpp`.
- Remove `src/ui/MainWindow.cpp` from `CMakeLists.txt`; add `src/ui/TetherView.cpp`.
- `src/main.cpp`: remove `#include "ui/MainWindow.h"`; the normal run path creates
  `RetouchWindow window; window.show();`. The `--undotest` path already uses
  `RetouchWindow` (unchanged). `--export-icon` path unchanged.
- Window title: `RetouchWindow` should carry the app title ("NikonTether").

## Data flow

1. App launches → `RetouchWindow` shows in **Retouch mode**.
2. User clicks **Tether** → stack shows `TetherView`, tether chrome appears.
3. User connects camera, toggles live view, captures (button or Space).
4. `TetherView` emits `captureComplete(path)` → window appends to filmstrip;
   user stays in Tether mode and keeps shooting.
5. User clicks a filmstrip thumbnail → window switches to **Retouch mode** and
   opens that photo in an editing tab.

## Error handling

- `TetherView::errorOccurred` → status bar message (same UX as today's
  `MainWindow::handleError`).
- Connection state changes toggle Connect/Disconnect/Live View/Capture action
  enablement (as today's `setConnectedState`).
- Switching modes must not tear down the camera connection or an in-progress
  live view — `TetherView` persists; only visibility changes.

## Testing

- **Build:** CMake builds with `TetherView` added and `MainWindow` removed.
- **`--undotest`:** still passes (unaffected).
- **Manual/behavioral checks:**
  - Launch → opens in Retouch mode.
  - Tether button → live view + camera controls visible; editing chrome hidden.
  - Capture (button and Space) adds to filmstrip; stays in Tether.
  - Click filmstrip thumbnail → switches to Retouch mode with photo open.
  - Retouch button → editing chrome visible; tether chrome hidden.
  - Camera connection survives mode switches.

## Out of scope

- No changes to camera I/O, RAW editing algorithms, export, or sidecar logic.
- No "remember last mode" persistence (startup is always Retouch).
- No redesign of the editing tools or the tether controls themselves.
