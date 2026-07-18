# Crop Drag Handles + Esc-Resets-Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 8 resize handles (corner brackets + edge ticks, with rule-of-thirds gridlines while dragging) to the crop rectangle, and make Esc deselect the active retouch tool.

**Architecture:** Part 1 extends the `ImageCanvas` Qt widget's interaction model (a `Drag::Resizing` state + a `Handle` enum, hit-testing in `mousePressEvent`, per-handle geometry in `mouseMoveEvent`, and handle/grid rendering in `paintEvent`). Part 2 extracts `RetouchWindow`'s existing tool-deselect block into a reusable method and binds it to an Escape `QShortcut`.

**Tech Stack:** C++17, Qt Widgets (QWidget/QPainter), CMake + Ninja build.

## Global Constraints

- No test framework exists; there are no unit tests. Each task is verified by **building** (`cmake --build build`) and **manual verification** in the running app (`./build/nikontether`). Do not add a test framework.
- Follow existing `ImageCanvas` conventions: crop selection lives in widget coords as `m_p0`/`m_p1`; all image mapping goes through `targetRect()`.
- Do not change `selectionInImage()`, the `cropSelected(QRect)` / `commitCropRequested()` signals, or the `RetouchTab` commit path.
- Aspect-ratio constraint helper is `constrainedCorner(const QPoint&)`; `m_cropAspect == 0` means freeform.
- Every commit message ends with:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

### Task 1: Add resize state, Handle enum, and hit-testing

Add the interaction state and a `handleAt()` hit-tester. No behavior change yet beyond starting a (not-yet-implemented) resize drag; this task ends at a clean build.

**Files:**
- Modify: `src/edit/ImageCanvas.h` (enum + members + method decl, around lines 66, 72, 83–87)
- Modify: `src/edit/ImageCanvas.cpp` (`handleAt()` helper + `mousePressEvent`, around lines 152, 270–282)

**Interfaces:**
- Produces: `enum class Handle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };`
- Produces: `Handle handleAt(const QPoint &pos) const;` — returns which handle zone `pos` falls in for the current `selectionRect()`, or `Handle::None`.
- Produces: `Drag::Resizing` enum value; members `Handle m_activeHandle`, `QRect m_rectAtDragStart`.

- [ ] **Step 1: Extend the Drag enum and add the Handle enum + members**

In `src/edit/ImageCanvas.h`, replace the enum on line 72:

```cpp
    enum class Drag { None, Creating, Moving, Resizing };
    enum class Handle { None, TopLeft, Top, TopRight, Right,
                        BottomRight, Bottom, BottomLeft, Left };
```

Then in the private members block (after `m_rectAtMoveStart;` on line 87) add:

```cpp
    Handle m_activeHandle = Handle::None;
    QRect m_rectAtDragStart; // selection rect (widget coords) captured at press
```

And declare the hit-tester next to `constrainedCorner` (after line 66):

```cpp
    Handle handleAt(const QPoint &pos) const; // which crop handle is under pos
```

- [ ] **Step 2: Implement handleAt() in the cpp**

In `src/edit/ImageCanvas.cpp`, add after `constrainedCorner()` (after line 168):

```cpp
ImageCanvas::Handle ImageCanvas::handleAt(const QPoint &pos) const {
    QRect r = selectionRect();
    if (r.isEmpty()) return Handle::None;
    const int t = 10; // grab tolerance in widget px
    auto near = [&](int a, int b) { return std::abs(a - b) <= t; };
    bool onLeft   = near(pos.x(), r.left());
    bool onRight  = near(pos.x(), r.right());
    bool onTop    = near(pos.y(), r.top());
    bool onBottom = near(pos.y(), r.bottom());
    // Only count edge hits when the other axis is within the rect span (± tol).
    bool inX = pos.x() >= r.left() - t && pos.x() <= r.right() + t;
    bool inY = pos.y() >= r.top() - t && pos.y() <= r.bottom() + t;
    if (onTop && onLeft)       return Handle::TopLeft;
    if (onTop && onRight)      return Handle::TopRight;
    if (onBottom && onLeft)    return Handle::BottomLeft;
    if (onBottom && onRight)   return Handle::BottomRight;
    if (onTop && inX)          return Handle::Top;
    if (onBottom && inX)       return Handle::Bottom;
    if (onLeft && inY)         return Handle::Left;
    if (onRight && inY)        return Handle::Right;
    return Handle::None;
}
```

- [ ] **Step 3: Hit-test handles first in mousePressEvent**

In `src/edit/ImageCanvas.cpp`, replace the crop-mode block (lines 270–282) with:

```cpp
    // Crop mode.
    if (m_cropMode && ev->button() == Qt::LeftButton) {
        Handle h = handleAt(ev->pos());
        if (h != Handle::None) {
            m_drag = Drag::Resizing;
            m_activeHandle = h;
            m_rectAtDragStart = selectionRect();
        } else if (selectionRect().contains(ev->pos())) {
            m_drag = Drag::Moving;
            m_moveStart = ev->pos();
            m_rectAtMoveStart = selectionRect();
            setCursor(Qt::ClosedHandCursor);
        } else {
            m_drag = Drag::Creating;
            m_p0 = m_p1 = ev->pos();
        }
        update();
        return;
    }
```

- [ ] **Step 4: Build and verify it compiles**

Run: `cmake --build build`
Expected: builds with no errors. (Grabbing a handle currently does nothing visible on move — implemented in Task 2 — but pressing on a handle must not crash.)

- [ ] **Step 5: Commit**

```bash
git add src/edit/ImageCanvas.h src/edit/ImageCanvas.cpp
git commit -m "Add crop resize state and handle hit-testing

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Implement resize geometry in mouseMoveEvent

Make grabbing a handle actually resize the crop, respecting the active aspect ratio and clamping to image bounds.

**Files:**
- Modify: `src/edit/ImageCanvas.cpp` (`mouseMoveEvent`, around lines 303–332; `mouseReleaseEvent` reset, around lines 334–341)

**Interfaces:**
- Consumes: `Drag::Resizing`, `Handle`, `m_activeHandle`, `m_rectAtDragStart`, `constrainedCorner()`, `targetRect()` (all from Task 1 / existing).

- [ ] **Step 1: Add the resize branch to mouseMoveEvent**

In `src/edit/ImageCanvas.cpp`, insert a new `else if` branch immediately after the `Drag::Moving` branch closes (after line 316, before `} else if (m_panning)`):

```cpp
    } else if (m_drag == Drag::Resizing) {
        QRect tr = targetRect();
        QRect r = m_rectAtDragStart;
        QPoint pos = ev->pos();
        // Move the edge(s) owned by the active handle to follow the cursor,
        // clamped to the image bounds.
        int L = r.left(), T = r.top(), R = r.right(), B = r.bottom();
        auto cx = [&](int x) { return std::clamp(x, tr.left(), tr.right()); };
        auto cy = [&](int y) { return std::clamp(y, tr.top(), tr.bottom()); };
        switch (m_activeHandle) {
            case Handle::Left:        L = cx(pos.x()); break;
            case Handle::Right:       R = cx(pos.x()); break;
            case Handle::Top:         T = cy(pos.y()); break;
            case Handle::Bottom:      B = cy(pos.y()); break;
            case Handle::TopLeft:     L = cx(pos.x()); T = cy(pos.y()); break;
            case Handle::TopRight:    R = cx(pos.x()); T = cy(pos.y()); break;
            case Handle::BottomLeft:  L = cx(pos.x()); B = cy(pos.y()); break;
            case Handle::BottomRight: R = cx(pos.x()); B = cy(pos.y()); break;
            case Handle::None:        break;
        }
        QRect nr = QRect(QPoint(L, T), QPoint(R, B)).normalized();

        if (m_cropAspect > 0) {
            // Preserve aspect: anchor the corner opposite the moving one and
            // reuse constrainedCorner (which anchors at m_p0). Edge handles are
            // treated as their adjacent "grow" corner.
            QPoint anchor, moving;
            switch (m_activeHandle) {
                case Handle::TopLeft:     anchor = r.bottomRight(); moving = nr.topLeft(); break;
                case Handle::TopRight:    anchor = r.bottomLeft();  moving = nr.topRight(); break;
                case Handle::BottomLeft:  anchor = r.topRight();    moving = nr.bottomLeft(); break;
                case Handle::BottomRight: anchor = r.topLeft();     moving = nr.bottomRight(); break;
                case Handle::Left:        anchor = r.bottomRight(); moving = QPoint(nr.left(), nr.top()); break;
                case Handle::Right:       anchor = r.topLeft();     moving = QPoint(nr.right(), nr.bottom()); break;
                case Handle::Top:         anchor = r.bottomRight(); moving = QPoint(nr.left(), nr.top()); break;
                case Handle::Bottom:      anchor = r.topLeft();     moving = QPoint(nr.right(), nr.bottom()); break;
                case Handle::None:        anchor = r.topLeft();     moving = nr.bottomRight(); break;
            }
            QPoint savedP0 = m_p0;
            m_p0 = anchor;                       // constrainedCorner anchors at m_p0
            QPoint c = constrainedCorner(moving);
            m_p0 = savedP0;
            nr = QRect(anchor, c).normalized();
        }

        m_p0 = nr.topLeft();
        m_p1 = nr.bottomRight();
        update();
    }
```

- [ ] **Step 2: Reset m_activeHandle on release**

In `src/edit/ImageCanvas.cpp`, in `mouseReleaseEvent`, update the drag-end block (lines 335–340) to clear the handle:

```cpp
    if (m_drag != Drag::None && ev->button() == Qt::LeftButton) {
        m_drag = Drag::None;
        m_activeHandle = Handle::None;
        setCursor(m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor);
        update();
        emit cropSelected(selectionInImage());
        return;
    }
```

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: builds with no errors.

- [ ] **Step 4: Manually verify resize**

Run: `./build/nikontether`, open a photo, enter Crop (press `C`), draw a crop, then:
- Drag each corner and each edge; the corresponding side(s) follow the cursor and stay within the image.
- Set aspect to `3:2` in the combo and drag a corner: the box keeps a 3:2 ratio.
- Set aspect to `Freeform`: edges move independently.

- [ ] **Step 5: Commit**

```bash
git add src/edit/ImageCanvas.cpp
git commit -m "Implement crop handle resize with aspect preservation

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Hover cursors for handles

Give directional resize cursors when hovering a handle in crop mode.

**Files:**
- Modify: `src/edit/ImageCanvas.cpp` (`mouseMoveEvent` idle-crop branch, around lines 325–327)

**Interfaces:**
- Consumes: `handleAt()` (Task 1).

- [ ] **Step 1: Replace the idle crop cursor branch**

In `src/edit/ImageCanvas.cpp`, replace the `else if (m_cropMode)` branch (lines 325–327) with:

```cpp
    } else if (m_cropMode) {
        Handle h = handleAt(ev->pos());
        Qt::CursorShape c = Qt::CrossCursor;
        switch (h) {
            case Handle::TopLeft:
            case Handle::BottomRight: c = Qt::SizeFDiagCursor; break;
            case Handle::TopRight:
            case Handle::BottomLeft:  c = Qt::SizeBDiagCursor; break;
            case Handle::Top:
            case Handle::Bottom:      c = Qt::SizeVerCursor; break;
            case Handle::Left:
            case Handle::Right:       c = Qt::SizeHorCursor; break;
            case Handle::None:
                c = selectionRect().contains(ev->pos()) ? Qt::SizeAllCursor
                                                        : Qt::CrossCursor;
                break;
        }
        setCursor(c);
    }
```

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: builds with no errors.

- [ ] **Step 3: Manually verify cursors**

Run: `./build/nikontether`, enter Crop, draw a crop, hover over each corner/edge/inside — the cursor changes to the matching diagonal / horizontal / vertical / move shape.

- [ ] **Step 4: Commit**

```bash
git add src/edit/ImageCanvas.cpp
git commit -m "Add directional hover cursors for crop handles

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Render corner brackets, edge ticks, and rule-of-thirds grid

Draw the visible handles and the drag-time gridlines.

**Files:**
- Modify: `src/edit/ImageCanvas.cpp` (`paintEvent` crop block, around lines 198–209)

**Interfaces:**
- Consumes: `selectionRect()`, `targetRect()`, `m_drag`, `m_cropMode` (existing).

- [ ] **Step 1: Extend the crop paint block**

In `src/edit/ImageCanvas.cpp`, replace the crop paint block (lines 198–209) with:

```cpp
    if (m_cropMode && (m_drag != Drag::None || !selectionRect().isEmpty())) {
        QRect sel = selectionRect().intersected(tr);
        if (!sel.isEmpty()) {
            QRegion outside(tr);
            outside = outside.subtracted(QRegion(sel));
            p.setClipRegion(outside);
            p.fillRect(tr, QColor(0, 0, 0, 120));
            p.setClipping(false);
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawRect(sel);

            // Rule-of-thirds gridlines, only while actively dragging.
            if (m_drag != Drag::None) {
                p.setPen(QPen(QColor(255, 255, 255, 90), 1));
                for (int i = 1; i <= 2; ++i) {
                    int x = sel.left() + sel.width() * i / 3;
                    int y = sel.top() + sel.height() * i / 3;
                    p.drawLine(x, sel.top(), x, sel.bottom());
                    p.drawLine(sel.left(), y, sel.right(), y);
                }
            }

            // Corner brackets (L-shapes) + edge ticks.
            const int leg = std::min(18, std::min(sel.width(), sel.height()) / 3);
            p.setPen(QPen(Qt::white, 2));
            const int l = sel.left(), t = sel.top(), r = sel.right(), b = sel.bottom();
            // corners
            p.drawLine(l, t, l + leg, t); p.drawLine(l, t, l, t + leg);
            p.drawLine(r, t, r - leg, t); p.drawLine(r, t, r, t + leg);
            p.drawLine(l, b, l + leg, b); p.drawLine(l, b, l, b - leg);
            p.drawLine(r, b, r - leg, b); p.drawLine(r, b, r, b - leg);
            // edge midpoint ticks
            int mx = (l + r) / 2, my = (t + b) / 2;
            p.drawLine(mx - leg / 2, t, mx + leg / 2, t);
            p.drawLine(mx - leg / 2, b, mx + leg / 2, b);
            p.drawLine(l, my - leg / 2, l, my + leg / 2);
            p.drawLine(r, my - leg / 2, r, my + leg / 2);
        }
    }
```

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: builds with no errors.

- [ ] **Step 3: Manually verify rendering**

Run: `./build/nikontether`, enter Crop, draw a crop:
- White L-brackets appear at the 4 corners and a short tick at each edge midpoint.
- Rule-of-thirds lines (2 vertical + 2 horizontal, faint) appear only **while dragging/resizing/creating** and disappear on release.
- Brackets stay inside the crop box at small sizes (leg shrinks).

- [ ] **Step 4: Commit**

```bash
git add src/edit/ImageCanvas.cpp
git commit -m "Draw crop corner brackets, edge ticks, and rule-of-thirds grid

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Esc resets the active tool

Extract `RetouchWindow`'s tool-deselect logic into a method and bind Escape to it.

**Files:**
- Modify: `src/edit/RetouchWindow.h` (declare `deselectAllTools()`, near line 48 in the `private:` block)
- Modify: `src/edit/RetouchWindow.cpp` (add `#include <QShortcut>`; add method; call it from `onTabChanged`; bind shortcut in constructor)

**Interfaces:**
- Produces: `void deselectAllTools();` — unchecks zoom/crop/heal/wb-pick buttons (with `QSignalBlocker`) and calls the matching `tab->set…Mode(false)`.

- [ ] **Step 1: Declare the method**

In `src/edit/RetouchWindow.h`, in the `private:` block (after `void buildToolPanel();` on line 49) add:

```cpp
    void deselectAllTools(); // uncheck all left-bar tools and exit their modes
```

- [ ] **Step 2: Add the include**

In `src/edit/RetouchWindow.cpp`, add after line 10 (`#include <QKeySequence>`):

```cpp
#include <QShortcut>
```

- [ ] **Step 3: Implement deselectAllTools()**

In `src/edit/RetouchWindow.cpp`, add this method (place it just before `RetouchWindow::onTabChanged` near line 586):

```cpp
void RetouchWindow::deselectAllTools() {
    RetouchTab *tab = currentTab();
    if (m_toolZoom) {
        QSignalBlocker b(m_toolZoom);
        m_toolZoom->setChecked(false);
    }
    if (tab) tab->setZoomMode(false);
    if (m_cropToggle) {
        QSignalBlocker b(m_cropToggle);
        m_cropToggle->setChecked(false);
    }
    if (m_cropApply) m_cropApply->setEnabled(false);
    if (m_wbPick) {
        QSignalBlocker b(m_wbPick);
        m_wbPick->setChecked(false);
    }
    if (tab) tab->setWbPickMode(false);
    if (m_healToggle) {
        QSignalBlocker b(m_healToggle);
        m_healToggle->setChecked(false);
    }
    if (tab) tab->setHealMode(false);
    if (m_toolOptionsBar) m_toolOptionsBar->setVisible(false);
}
```

- [ ] **Step 4: Call it from onTabChanged**

In `src/edit/RetouchWindow.cpp`, in `onTabChanged`, replace the inlined deselect block (lines 590–610, from `m_toolOptionsBar->setVisible(false);` through `if (tab) tab->setHealMode(false);`) with a single call. The result should read:

```cpp
void RetouchWindow::onTabChanged(int) {
    RetouchTab *tab = currentTab();
    bool ready = tab && tab->isReady();
    setDockEnabled(ready);
    deselectAllTools();
    m_undoAction->setEnabled(tab && tab->canUndo());
    m_redoAction->setEnabled(tab && tab->canRedo());
    syncDockFromTab();
```

(Leave the rest of `onTabChanged` — from `if (ready) {` onward — unchanged.)

- [ ] **Step 5: Bind Escape in the constructor**

In `src/edit/RetouchWindow.cpp`, add at the end of the constructor body (just before its closing `}`; the constructor starts at line 90):

```cpp
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WindowShortcut);
    connect(escShortcut, &QShortcut::activated, this, &RetouchWindow::deselectAllTools);
```

- [ ] **Step 6: Build**

Run: `cmake --build build`
Expected: builds with no errors.

- [ ] **Step 7: Manually verify**

Run: `./build/nikontether`, open a photo. For each tool — Zoom (`Z`), Crop (`C`), Spot Heal (`H`), and the WB eyedropper — activate it, then press `Esc`: the tool button un-checks, the contextual options row hides, and the window returns to idle. Confirm switching tabs still starts with no tool active.

- [ ] **Step 8: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "Reset active retouch tool on Escape

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Full manual verification pass

Run the whole feature end-to-end against the spec's testing checklist.

**Files:** none (verification only).

- [ ] **Step 1: Build clean**

Run: `cmake --build build`
Expected: no errors.

- [ ] **Step 2: Walk the spec checklist**

Run: `./build/nikontether`, open a photo, and confirm all of:
1. Draw a crop; grab each of the 8 handles — resize goes in the expected direction; hover cursor matches.
2. Aspect `3:2` active → handle resize preserves ratio; `Freeform` → independent edge movement.
3. Rule-of-thirds gridlines appear only while dragging and vanish on release.
4. Corner brackets + edge ticks render and stay within the crop box.
5. Each tool (zoom/crop/heal/wb) + `Esc` → returns to idle.
6. `Enter` still commits a crop; `Esc` discards an un-applied crop selection.

- [ ] **Step 3: Confirm no regressions**

Confirm create-from-scratch and move-whole-box still work, and that committing a crop (Apply / Enter) produces the same result as before.

---

## Notes on interaction precedence (reference)

`mousePressEvent` crop precedence after this plan: **handle → inside (move) → outside (create)**. `handleAt()` uses a 10px tolerance; because corners are checked before edges, a corner grab wins near the corner. Resizing mutates only `m_p0`/`m_p1`, so `selectionInImage()` and the commit path are untouched.
