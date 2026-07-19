# Photoshop-style Subtool Flyout — Design

**Date:** 2026-07-19
**Status:** Approved

## Problem

Tools that have multiple usages (currently only local adjustment masks:
radial / graduated / brush) expose their sub-modes only inside the mask panel
dock. There's no fast, discoverable way to switch sub-mode from the tool
sidebar. Photoshop solves this with a flyout drawer next to the tool icon.

## Goal

Add a generic, reusable flyout mechanism so a sidebar tool can carry a set of
subtools, selectable via a Photoshop-style long-press flyout. Wire it for the
Masks tool and remove the duplicate add-buttons from the mask panel.

## Audit result

The only tool with discrete subtools today is **Masks** (three `MaskType`
values: Radial / Linear / Brush). Zoom / Crop / Heal use a contextual
`QStackedWidget` options bar for parameters, not discrete sub-modes, so they are
out of scope. The flyout mechanism is built generically for future opt-in.

## Design

### 1. Generic subtool descriptor
```cpp
struct SubTool {
    int id;                          // e.g. static_cast<int>(MaskType)
    QPixmap (*draw)(const QColor&);  // programmatic glyph, matches tool-icon style
    QString label;                   // "Radial"
    QString tooltip;                 // "Radial mask"
};
```
A tool with a non-empty `QVector<SubTool>` is a "flyout tool."

### 2. `ToolFlyout` widget (new)
Frameless `Qt::Popup` `QWidget` presenting a horizontal strip of subtool icon
buttons, positioned adjacent to the owning tool button. Emits
`subToolChosen(int id)`. Dismisses on pick or click-away (default `Qt::Popup`).

### 3. Long-press interaction
The mask `QToolButton` gets press/hold detection:
- **Quick click** → activate tool and apply the *active* subtool (adds a mask of
  the current type — equivalent to the old per-type "+" button).
- **Press & hold (~400 ms)** → open the flyout; picking a subtool sets it active
  and applies it.
- A small corner triangle drawn on the tool icon signals "has a flyout."

### 4. Active-subtool state & icon
Each flyout tool remembers its active subtool. The tool button glyph reflects
the active subtool, so the sidebar communicates what a plain click will do.

### 5. MaskPanel change
Remove `m_addRadial / m_addLinear / m_addBrush` buttons from the panel. The
panel becomes edit-only (mask list combo + shape/adjust sliders). Adding flows
through the flyout / tool click → `RetouchWindow` → existing
`currentTab()->addMask(type)`.

### 6. Wiring
Keep existing manual mutual-exclusion between sidebar tools. The flyout is
per-tool. Only Masks is wired now.

## Files touched
- `src/ui/ToolFlyout.{h,cpp}` (new)
- `src/edit/RetouchWindow.{h,cpp}` — SubTool model, long-press handling on
  mask button, flyout wiring, corner-triangle icon, active-subtool state.
- `src/ui/MaskPanel.{h,cpp}` — remove add-buttons + `addMaskRequested` emission.
- Build file (CMake/qmake) — register new source files.

## Non-goals
- No changes to Zoom / Crop / Heal.
- No changes to the mask model or adjustment math.
