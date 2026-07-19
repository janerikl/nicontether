# Filmstrip Thumbnail Delete Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a right-click "Delete" action to filmstrip thumbnails that moves the capture file(s) (and their edit sidecars) to the OS trash and removes them from the running app.

**Architecture:** `FilmstripWidget` gains a "Delete" entry in its existing `contextMenuEvent` and a new `deleteRequested(QStringList)` signal. `RetouchWindow` connects a slot that trashes each file/sidecar via `QFile::moveToTrash`, then removes the thumbnail, the `m_filmstripPaths` entry, and any open editor tab. Follows the existing `retouchRequested`/`syncEditsRequested` pattern exactly — no new context-menu policy.

**Tech Stack:** C++, Qt Widgets (`QListWidget`, `QMenu`, `QFile::moveToTrash`).

## Global Constraints

- Move files to the OS trash (recoverable) via `QFile::moveToTrash()` — never `QFile::remove()`.
- No confirmation dialog; delete happens immediately on menu-item click.
- Multi-select rule: if the right-clicked item is in the current selection, delete all selected paths; otherwise delete only the clicked path.
- Menu label reflects count: "Delete Photo" (one) / "Delete N Photos" (many).
- Match existing filmstrip menu conventions in `src/ui/FilmstripWidget.cpp:95-111`.

---

### Task 1: FilmstripWidget emits deleteRequested with the correct target set

**Files:**
- Modify: `src/ui/FilmstripWidget.h:22-25` (signals block)
- Modify: `src/ui/FilmstripWidget.cpp:95-111` (`contextMenuEvent`)

**Interfaces:**
- Produces: `void FilmstripWidget::deleteRequested(const QStringList &paths)` — emitted with one or more absolute file paths when the user chooses Delete from the thumbnail context menu.

- [ ] **Step 1: Add the signal**

In `src/ui/FilmstripWidget.h`, add to the `signals:` block (after line 25):

```cpp
    void deleteRequested(const QStringList &paths); // "Delete" chosen from the menu
```

- [ ] **Step 2: Add the Delete action and target-selection logic**

Replace `contextMenuEvent` in `src/ui/FilmstripWidget.cpp` (lines 95-111) with:

```cpp
void FilmstripWidget::contextMenuEvent(QContextMenuEvent *ev) {
    QListWidgetItem *item = itemAt(ev->pos());
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();

    // If the clicked thumbnail is part of the current multi-selection, the
    // action targets the whole selection; otherwise just the clicked one.
    QStringList delTargets;
    if (item->isSelected() && selectedItems().size() > 1)
        delTargets = selectedPaths();
    else
        delTargets = QStringList{path};

    QMenu menu(this);
    QAction *retouch = menu.addAction("Open in Retouch");
    const int selCount = selectedItems().size();
    QAction *sync = menu.addAction(
        selCount > 1 ? QString("Sync Edits to %1 Selected").arg(selCount)
                     : QString("Sync Edits to Selected"));
    QAction *del = menu.addAction(
        delTargets.size() > 1 ? QString("Delete %1 Photos").arg(delTargets.size())
                              : QString("Delete Photo"));
    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == retouch)
        emit retouchRequested(path);
    else if (chosen == sync)
        emit syncEditsRequested();
    else if (chosen == del)
        emit deleteRequested(delTargets);
}
```

- [ ] **Step 3: Build to verify it compiles**

Run the project's configured build (CMake). Example:

```bash
cmake --build build 2>&1 | tail -20
```

Expected: builds with no errors relating to `FilmstripWidget`. (If no `build/` dir exists, configure with `cmake -B build` first.)

- [ ] **Step 4: Commit**

```bash
git add src/ui/FilmstripWidget.h src/ui/FilmstripWidget.cpp
git commit -m "feat: emit deleteRequested from filmstrip context menu"
```

---

### Task 2: RetouchWindow trashes files and removes thumbnails on deleteRequested

**Files:**
- Modify: `src/edit/RetouchWindow.h:54-58` (private slots), `:60` (includes if needed)
- Modify: `src/edit/RetouchWindow.cpp:325-330` (connect), add slot near `onTabCloseRequested` (`:1295`)

**Interfaces:**
- Consumes: `FilmstripWidget::deleteRequested(const QStringList &paths)` from Task 1; `EditSidecar::pathFor` / `EditSidecar::exists` (`src/edit/EditSidecar.h`); members `m_filmstrip`, `m_filmstripPaths` (`QSet<QString>`), `m_openTabs` (`QMap<QString, RetouchTab*>`), `m_tabs` (`QTabWidget*`), `m_statusLabel` (`QLabel*`).

- [ ] **Step 1: Declare the slot**

In `src/edit/RetouchWindow.h`, add to the `private slots:` block (after line 58):

```cpp
    void onDeleteRequested(const QStringList &paths);
```

- [ ] **Step 2: Ensure QFile is available**

At the top of `src/edit/RetouchWindow.cpp`, confirm/add:

```cpp
#include <QFile>
```

(Add it alongside the other Qt includes if not already present.)

- [ ] **Step 3: Wire the connection**

In `src/edit/RetouchWindow.cpp`, after the `syncEditsRequested` connect (line 327-328), add:

```cpp
    connect(m_filmstrip, &FilmstripWidget::deleteRequested, this,
            &RetouchWindow::onDeleteRequested);
```

- [ ] **Step 4: Implement the slot**

Add after `onTabCloseRequested` (after line 1301) in `src/edit/RetouchWindow.cpp`:

```cpp
void RetouchWindow::onDeleteRequested(const QStringList &paths) {
    int deleted = 0, failed = 0;
    for (const QString &path : paths) {
        // Trash the RAW; skip UI/state removal if the file can't be trashed so
        // we never drop a thumbnail while its file remains on disk.
        if (!QFile::moveToTrash(path)) {
            ++failed;
            continue;
        }
        // Best-effort trash of the edit sidecar (may not exist).
        if (EditSidecar::exists(path))
            QFile::moveToTrash(EditSidecar::pathFor(path));

        // Close an open editor tab for this photo, if any.
        if (RetouchTab *tab = m_openTabs.value(path, nullptr)) {
            int idx = m_tabs->indexOf(tab);
            if (idx >= 0) m_tabs->removeTab(idx);
            m_openTabs.remove(path);
            tab->deleteLater();
        }

        // Remove the filmstrip thumbnail (match by UserRole path).
        for (int i = 0; i < m_filmstrip->count(); ++i) {
            QListWidgetItem *it = m_filmstrip->item(i);
            if (it->data(Qt::UserRole).toString() == path) {
                delete m_filmstrip->takeItem(i);
                break;
            }
        }
        m_filmstripPaths.remove(path);
        ++deleted;
    }

    if (failed > 0)
        m_statusLabel->setText(
            QString("Deleted %1 photo(s); %2 could not be moved to Trash")
                .arg(deleted).arg(failed));
    else
        m_statusLabel->setText(QString("Deleted %1 photo(s)").arg(deleted));
}
```

Note: `RetouchWindow.cpp` already includes `edit/EditSidecar.h` (line 5) and uses `m_filmstrip->item(i)`/`count()` patterns (lines 1094, 1373). `QListWidgetItem` is available via the `FilmstripWidget`/`QListWidget` includes already used in this file.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: builds with no errors.

- [ ] **Step 6: Manual verification**

Launch the app with a session containing several captures and confirm:
- Right-click a single thumbnail → Delete Photo removes it; the `.nef` (and `.nte.json` if present) appear in the OS trash.
- Ctrl-select several, right-click one of them → "Delete N Photos" removes all selected.
- With several selected, right-click a *different, unselected* thumbnail → only that one is deleted.
- Delete a photo whose editor tab is open → the tab closes.
- Status bar shows "Deleted N photo(s)".

- [ ] **Step 7: Commit**

```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "feat: move filmstrip photos to trash on delete"
```

---

## Self-Review

- **Spec coverage:** Trash-move ✓ (Task 2 Step 4). No confirmation ✓ (no dialog anywhere). Multi-select rule ✓ (Task 1 Step 2). Count-based label ✓ (Task 1 Step 2). Sidecar removal ✓. Open-tab teardown ✓. moveToTrash-failure skip + status message ✓ (Task 2 Step 4). Out-of-scope items (undo, Delete-key shortcut, permanent delete) correctly absent.
- **Placeholder scan:** No TBD/TODO; all code shown in full.
- **Type consistency:** `deleteRequested(const QStringList&)` defined in Task 1, consumed in Task 2; member types (`m_filmstripPaths` QSet, `m_openTabs` QMap, `m_tabs` QTabWidget, `m_statusLabel` QLabel) match `RetouchWindow.h:74-77,154`; `EditSidecar::pathFor/exists` match `EditSidecar.h:11-12`.

No automated unit test is added: the deliverable is Qt widget + OS-trash side effects with no clean pure seam (path-selection lives inline in the event handler, consistent with the existing untested `contextMenuEvent`). Verification is the manual checklist in Task 2 Step 6, matching the spec's testing note.
