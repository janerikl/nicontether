# Tether Session Prompting & Session Title

**Date:** 2026-07-19

## Problem

Entering Tether mode silently reuses a default `"studio"` session created at
startup, so captures land in a folder the user never named. Loading a session
from **File → Open Session** only fills the filmstrip — it does not become the
capture destination, so captures still go to the default folder. There is also
no visible indication of which session is currently active.

## Goals

1. Entering Tether mode prompts for a session name when no session is active.
2. Loading a session from the File menu adopts it as the active capture
   destination (and satisfies the "session is active" condition).
3. The window title reflects the active session.

## Behavior

- **No default session.** `TetherView` starts with no active session; the
  startup `startSession("studio")` is removed.
- **Prompt on entering Tether.** When the user clicks the **Tether** mode
  button and no session is active, prompt for a session name first.
  - On **Cancel**, abort the switch and remain in Retouch (revert the mode
    toolbar action).
  - If a session is already active — including one loaded from the File menu —
    enter Tether silently with no prompt.
- **Open Session adopts the capture destination.** `File → Open Session` and
  recent-session clicks point the tether capture directory at the chosen
  folder and mark that session active, so subsequent captures land there and
  entering Tether will not re-prompt.
- **Window title.** Shows `NikonTether — <name> (<yyyy-MM-dd>)` when a session
  is active, and `NikonTether` when none is.
- **Calibration unaffected.** The calibration flow calls `setMode(Tether)`
  programmatically; that path does not prompt (calibration performs no
  captures), so it works with or without an active session.

## Changes by file

### `SessionManager` (`src/capture/SessionManager.{h,cpp}`)
- Track active state and a display name.
- `bool isActive() const` — whether a session has been started/adopted.
- `QString currentName() const` — display name for the title.
- `void adoptDirectory(const QString &dir)` — mark an existing folder as the
  active session without minting a new dated folder; derive the display name
  from the folder name.
- `startSession()` sets active state and stores the display name.

### `TetherView` (`src/ui/TetherView.{h,cpp}`)
- Remove the constructor's `m_session.startSession("studio")` call and its
  `setSaveDirectory` seeding; start with no session.
- `bool hasSession() const` — proxies `m_session.isActive()`.
- `bool promptNewSession()` — shows the name dialog, starts the session, sets
  the controller save directory, emits `sessionChanged`; returns `false` on
  cancel.
- `void adoptSession(const QString &dir)` — adopt an existing folder as the
  capture destination and emit `sessionChanged`.
- New signal `void sessionChanged(const QString &name)` emitted whenever the
  active session changes (new, adopted).
- `onNewSession()` reuses `promptNewSession()`.

### `RetouchWindow` (`src/edit/RetouchWindow.cpp`)
- Tether-mode toolbar handler: if `!m_tetherView->hasSession()`, call
  `promptNewSession()`; on cancel, re-check the Retouch action and return
  without switching. Otherwise `setMode(Mode::Tether)`.
- `loadSession(dir)` calls `m_tetherView->adoptSession(dir)` in addition to
  populating the filmstrip.
- Connect `TetherView::sessionChanged` to an `updateWindowTitle()` helper that
  composes the title from the active session name.

## Out of scope

- Persisting the active session across app restarts.
- Multiple concurrent sessions.
