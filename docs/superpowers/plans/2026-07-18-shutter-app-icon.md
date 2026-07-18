# Shutter App Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give NikonTether a code-drawn steel-blue camera-shutter icon that shows in the desktop dock/taskbar and in a pinned launcher entry.

**Architecture:** A single function `makeShutterIcon()` paints a 6-blade shutter iris into a multi-size `QIcon` with QPainter (no image assets, no new Qt modules). `main.cpp` sets it as the window icon and gains an `--export-icon <path>` mode that writes the same artwork to a PNG. A `.desktop` file plus CMake install rules register the launcher entry, generating its PNG from the binary.

**Tech Stack:** C++17, Qt6 Widgets, CMake + Ninja.

## Global Constraints

- C++ standard: C++17 (`set(CMAKE_CXX_STANDARD 17)`).
- No new library dependencies — only `Qt6::Widgets` / `Qt6::Concurrent`, already linked. Do NOT add `Qt6::Svg`.
- Executable/binary name: `nikontether`. Existing source lives under `src/`, headers included via `target_include_directories(... PRIVATE src)`.
- Blade palette: graphite `QColor(48, 54, 62)` blades with steel-blue accent `QColor(70, 110, 150)`; transparent background.
- Icon sizes to render: 16, 32, 64, 128, 256 px.

---

### Task 1: Draw the shutter icon and set it as the window icon

**Files:**
- Create: `src/ui/AppIcon.h`
- Create: `src/ui/AppIcon.cpp`
- Modify: `src/main.cpp:11-13` (after `QApplication app(...)` / `setApplicationName`)
- Modify: `CMakeLists.txt` (add `src/ui/AppIcon.cpp` to `add_executable`)

**Interfaces:**
- Produces: `QIcon makeShutterIcon();` (declared in `src/ui/AppIcon.h`) — returns a multi-size shutter `QIcon`.

- [ ] **Step 1: Create the header**

`src/ui/AppIcon.h`:
```cpp
#pragma once

#include <QIcon>

// Builds the application shutter icon (6-blade iris) at multiple sizes.
QIcon makeShutterIcon();
```

- [ ] **Step 2: Create the drawing implementation**

`src/ui/AppIcon.cpp`:
```cpp
#include "AppIcon.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRadialGradient>
#include <QtMath>

namespace {

const QColor kGraphite(48, 54, 62);
const QColor kSteel(70, 110, 150);

void paintShutter(QPixmap &pm) {
    const qreal S = pm.width();
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c(S / 2.0, S / 2.0);
    const qreal R = S * 0.46;   // visible disc radius
    const qreal Ro = S * 0.60;  // blade outer reach (clipped to R)
    const qreal Ri = S * 0.17;  // central opening radius
    const int blades = 6;

    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p.setClipPath(disc);

    for (int i = 0; i < blades; ++i) {
        const qreal a0 = qDegreesToRadians(360.0 * i / blades);
        const qreal a1 = qDegreesToRadians(360.0 * (i + 1) / blades);

        const QPointF outerA(c.x() + Ro * qCos(a0), c.y() + Ro * qSin(a0));
        const QPointF outerB(c.x() + Ro * qCos(a1), c.y() + Ro * qSin(a1));
        const QPointF innerB(c.x() + Ri * qCos(a1), c.y() + Ri * qSin(a1));

        QPolygonF blade;
        blade << outerA << outerB << innerB;

        QLinearGradient g(outerA, innerB);
        g.setColorAt(0.0, kGraphite);
        g.setColorAt(1.0, kSteel);

        p.setPen(QPen(kGraphite.darker(140), qMax(1.0, S / 128.0)));
        p.setBrush(g);
        p.drawPolygon(blade);
    }

    // Steel glow in the opening.
    QRadialGradient rg(c, Ri * 1.3);
    rg.setColorAt(0.0, QColor(90, 140, 190, 90));
    rg.setColorAt(1.0, QColor(90, 140, 190, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(rg);
    p.drawEllipse(c, Ri * 1.3, Ri * 1.3);

    // Rim ring for definition.
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kSteel.darker(120), qMax(1.0, S / 64.0)));
    p.drawEllipse(c, R, R);

    p.end();
}

}  // namespace

QIcon makeShutterIcon() {
    QIcon icon;
    for (int sz : {16, 32, 64, 128, 256}) {
        QPixmap pm(sz, sz);
        paintShutter(pm);
        icon.addPixmap(pm);
    }
    return icon;
}
```

- [ ] **Step 3: Register the source file in CMake**

In `CMakeLists.txt`, inside `add_executable(nikontether ...)`, add alongside the other `src/ui/*.cpp` entries:
```cmake
    src/ui/AppIcon.cpp
```

- [ ] **Step 4: Set the window icon in main**

In `src/main.cpp`, add the include near the top (after `#include "edit/RetouchTab.h"`):
```cpp
#include "ui/AppIcon.h"
```
Then, immediately after the existing `app.setOrganizationName("NikonTether");` line:
```cpp
    app.setWindowIcon(makeShutterIcon());
```

- [ ] **Step 5: Configure and build**

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: build succeeds, links `nikontether` with no errors.

- [ ] **Step 6: Verify the icon appears (visual)**

Run: `./build/nikontether`
Expected: the main window opens; its title-bar/taskbar/dock entry shows the steel-blue shutter iris (not the generic default icon). Close the window.

- [ ] **Step 7: Commit**

```bash
git add src/ui/AppIcon.h src/ui/AppIcon.cpp src/main.cpp CMakeLists.txt
git commit -m "Add code-drawn shutter app icon and set as window icon"
```

---

### Task 2: Add the `--export-icon` PNG export mode

**Files:**
- Modify: `src/main.cpp` (add CLI branch after `QApplication` construction, before the `--undotest` branch)

**Interfaces:**
- Consumes: `makeShutterIcon()` from Task 1.
- Produces: CLI contract `nikontether --export-icon <path>` writes a 256px PNG and exits 0; invalid/missing path exits non-zero.

- [ ] **Step 1: Add the export branch**

In `src/main.cpp`, after `app.setWindowIcon(makeShutterIcon());` and before the existing `if (argc >= 3 && ... "--undotest" ...)` block, insert:
```cpp
    if (argc >= 2 && std::strcmp(argv[1], "--export-icon") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: nikontether --export-icon <path.png>\n");
            return 2;
        }
        QPixmap pm = makeShutterIcon().pixmap(256, 256);
        if (!pm.save(argv[2], "PNG")) {
            fprintf(stderr, "error: could not write icon to %s\n", argv[2]);
            return 1;
        }
        return 0;
    }
```
(`QPixmap` is already available transitively; add `#include <QPixmap>` under the other includes in `main.cpp` if the build complains.)

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: build succeeds.

- [ ] **Step 3: Verify export succeeds**

Run: `./build/nikontether --export-icon /tmp/shutter.png && file /tmp/shutter.png`
Expected: exit 0; `file` reports `PNG image data, 256 x 256`. Open `/tmp/shutter.png` and confirm it shows the steel-blue shutter iris.

- [ ] **Step 4: Verify error path**

Run: `./build/nikontether --export-icon; echo "exit=$?"`
Expected: prints the usage message to stderr and `exit=2`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "Add --export-icon mode to render shutter PNG from icon code"
```

---

### Task 3: Add the `.desktop` launcher entry and install rules

**Files:**
- Create: `packaging/nikontether.desktop`
- Modify: `CMakeLists.txt` (append install rules after the existing `install(TARGETS ...)` line)

**Interfaces:**
- Consumes: `nikontether --export-icon <path>` from Task 2 (used at install time to generate the launcher PNG).

- [ ] **Step 1: Create the desktop entry**

`packaging/nikontether.desktop`:
```ini
[Desktop Entry]
Type=Application
Name=NikonTether
Comment=Tethered capture for Nikon cameras
Exec=nikontether
Icon=nikontether
Terminal=false
Categories=Graphics;Photography;
```

- [ ] **Step 2: Add install rules to CMake**

In `CMakeLists.txt`, after the existing `install(TARGETS nikontether RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})` line, append:
```cmake
# Desktop launcher entry.
install(FILES packaging/nikontether.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Generate the launcher PNG from the same drawing code as the window icon.
# Uses the offscreen Qt platform so it works on headless installs.
install(CODE "
    set(ICON_DIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps\")
    file(MAKE_DIRECTORY \"\${ICON_DIR}\")
    execute_process(
        COMMAND \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/nikontether\" --export-icon \"\${ICON_DIR}/nikontether.png\"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(WARNING \"Failed to generate nikontether.png (code \${_rc})\")
    endif()
")
```
The `install(CODE ...)` runs after the binary is installed, so the binary is already at its install path when invoked.

- [ ] **Step 3: Reconfigure (picks up new install rules)**

Run: `cmake -S . -B build -G Ninja`
Expected: configure succeeds with no errors.

- [ ] **Step 4: Verify install into a throwaway prefix**

Run:
```bash
QT_QPA_PLATFORM=offscreen DESTDIR=/tmp/nt-install cmake --install build --prefix /usr/local
find /tmp/nt-install -name 'nikontether.desktop' -o -name 'nikontether.png'
file /tmp/nt-install/usr/local/share/icons/hicolor/256x256/apps/nikontether.png
```
Expected: both the `.desktop` file and `nikontether.png` are listed; `file` reports `PNG image data, 256 x 256`.

- [ ] **Step 5: Commit**

```bash
git add packaging/nikontether.desktop CMakeLists.txt
git commit -m "Install .desktop launcher and generated shutter icon"
```

---

## Notes for the implementer

- If Step 6 of Task 1 runs on a headless machine, prefix with `QT_QPA_PLATFORM=offscreen` — the window won't be visible but the build/link is still verified; do the visual check on a machine with a display.
- Under some Wayland compositors the per-window icon is not shown in the dock; the `.desktop` entry from Task 3 is what guarantees a dock/menu icon there. Both mechanisms are intentionally included.
