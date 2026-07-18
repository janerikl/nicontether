# Shutter App Icon — Design

**Date:** 2026-07-18
**Project:** NikonTether (C++/Qt6 Linux tethered-capture app)

## Goal

Give the application a recognizable icon — a camera **shutter iris** — so it
displays a shutter when the running window appears in the desktop dock/taskbar,
and (via a launcher entry) when pinned to a dock or shown in the app menu.

## Approach

Draw the icon at runtime with **QPainter** into a `QIcon`, rather than shipping a
bitmap or SVG asset.

Rationale:
- **No new dependency.** SVG `QIcon` loading needs the `Qt6::Svg` plugin, which
  is not currently linked. Painting uses only `Qt6::Widgets`, already present.
- **No asset/resource files or paths.** The icon is generated in code and lives
  in the binary; nothing to install or locate at runtime.
- **Crisp at every size.** The icon is rendered into multiple pixmap sizes
  (16/32/64/128/256 px) so taskbar, dock, and alt-tab each get a sharp version.

## The drawing

A classic **6-blade camera shutter**: six straight-edged blades sweeping around
a central hexagonal opening — the recognizable iris look.

- **Blades:** graphite (dark neutral) with a subtle radial shade for depth.
- **Accent:** steel-blue tint on the blades / center opening so it pops at small
  dock sizes.
- **Background:** transparent, so it sits on any light or dark dock theme.
- Antialiased; drawn in a normalized coordinate space then scaled per size.

## Components

- **`src/ui/AppIcon.h` / `src/ui/AppIcon.cpp`** — one public function:
  - `QIcon makeShutterIcon();` — builds the multi-size `QIcon`.
  - Internal helper paints one blade set into a given `QPixmap` size.
  - Single source of truth for the shutter artwork.

- **`src/main.cpp`**
  - After constructing `QApplication`, call
    `app.setWindowIcon(makeShutterIcon());`.
  - Add a small CLI mode `--export-icon <path>`: render the 256px pixmap and
    save it as PNG to `<path>`, then exit. Used to generate the launcher PNG
    from the exact same drawing code (no drift between code icon and file icon).

- **`CMakeLists.txt`**
  - Add `src/ui/AppIcon.cpp` to the `add_executable(nikontether ...)` list.

- **Launcher integration** (desktop entry)
  - `packaging/nikontether.desktop` — a freedesktop `.desktop` entry
    (`Name=NikonTether`, `Exec=nikontether`, `Icon=nikontether`,
    `Categories=Graphics;Photography;`).
  - PNG generated via `nikontether --export-icon`, installed to the hicolor
    icon theme path.
  - CMake `install()` rules: install the `.desktop` file to
    `${CMAKE_INSTALL_DATADIR}/applications` and the exported PNG to
    `${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps/nikontether.png`.
    Generate the PNG at install time by invoking the built binary with
    `--export-icon`.

## Data flow

`makeShutterIcon()` is pure (no I/O, no camera/session state). It is called once
at startup for the window icon, and once per `--export-icon` invocation to write
the PNG. No interaction with the camera worker, session, or edit pipeline.

## Error handling

- `--export-icon` with a missing/invalid path: print an error to stderr, return
  non-zero exit code, do not open the GUI.
- Icon drawing cannot fail at runtime (in-memory painting); no error path needed
  in the normal startup call.

## Testing / verification

- Build succeeds with the new file listed in CMake.
- Launch the app; confirm the shutter icon appears on the window title bar and
  in the dock/taskbar of the running window.
- Run `nikontether --export-icon /tmp/shutter.png` and visually confirm the PNG
  shows the steel-blue shutter iris.
- (If installed) confirm the `.desktop` entry appears in the app menu with the
  shutter icon.

## Out of scope

- macOS/Windows packaging (this is a Linux Qt app).
- Animated / state-changing icons.
- App-wide theming or a full icon set beyond the app/launcher icon.
