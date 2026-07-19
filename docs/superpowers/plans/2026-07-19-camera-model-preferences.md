# Camera Model Preferences Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a File → Preferences… dialog (Ctrl+,) with a camera-model dropdown and AF-frame size fields, remembering AF calibration per model and auto-selecting the model from the connected camera.

**Architecture:** A pure `CameraModels` table (id, display, default AF frame, name matcher) drives a new `PreferencesDialog` that persists AF frame per model via QSettings and emits `afFrameSizeChanged`. The AF spin boxes move out of `ControlsPanel` into the dialog. `RetouchWindow` owns the dialog, wires it to `TetherView::setAfFrameSize`, and auto-selects the model from `TetherView::cameraConnected`.

**Tech Stack:** C++17, Qt6 (Widgets), CMake. QSettings scope org/app "NikonTether".

## Global Constraints

- C++ standard: C++17. New sources go in the `nikontether` target in `CMakeLists.txt`; new test executables use `add_executable` + `add_test`.
- QSettings default constructor (org/app "NikonTether"). Keys: `af/currentModel` (string), `af/models/<id>/frameWidth`, `af/models/<id>/frameHeight` (ints).
- Nominal per-model default AF frame is 640 × 426 (a calibration starting point; libgphoto2 does not expose the true value).
- Follow existing patterns: menu actions built in `RetouchWindow` like the File menu (RetouchWindow.cpp:257); `TetherView` re-exposes controller signals.

---

### Task 1: CameraModels table + matcher (pure) + test

**Files:**
- Create: `src/camera/CameraModels.h`
- Create: `tests/CameraModelsTest.cpp`
- Modify: `CMakeLists.txt` (add a second test target after `af_mapping_test`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct cammodel::Model { const char *id; const char *display; int afFrameW; int afFrameH; };`
  - `const std::vector<cammodel::Model>& cammodel::models();`
  - `const cammodel::Model* cammodel::byId(const std::string& id);` (nullptr if unknown)
  - `std::string cammodel::matchModel(const std::string& cameraName);` ("" if no match)

- [ ] **Step 1: Write the failing test**

Create `tests/CameraModelsTest.cpp`:

```cpp
#include "camera/CameraModels.h"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace cammodel;

    // The list is non-empty and includes the sentinel "custom" entry.
    assert(!models().empty());
    assert(byId("custom") != nullptr);

    // D7500 is present with positive default dimensions.
    const Model* m = byId("d7500");
    assert(m != nullptr);
    assert(m->afFrameW > 0 && m->afFrameH > 0);

    // Every model has positive defaults and a non-empty id/display.
    for (const Model& e : models()) {
        assert(e.id && e.id[0]);
        assert(e.display && e.display[0]);
        assert(e.afFrameW > 0 && e.afFrameH > 0);
    }

    // Name matching against gphoto2-style camera names.
    assert(matchModel("Nikon DSC D7500") == "d7500");
    assert(matchModel("Nikon DSC D750 (PTP mode)") == "d750");
    assert(matchModel("Canon EOS 5D") == "");
    assert(matchModel("") == "");

    // Unknown id -> nullptr.
    assert(byId("nope") == nullptr);

    std::puts("CameraModelsTest: all assertions passed");
    return 0;
}
```

- [ ] **Step 2: Add the CMake test target**

In `CMakeLists.txt`, immediately after the existing `add_test(NAME af_mapping_test ...)` line, add:

```cmake
add_executable(cam_models_test tests/CameraModelsTest.cpp)
target_include_directories(cam_models_test PRIVATE src)
add_test(NAME cam_models_test COMMAND cam_models_test)
```

- [ ] **Step 3: Run the test to verify it fails to build**

Run:
```bash
cmake -S . -B build >/dev/null && cmake --build build --target cam_models_test
```
Expected: FAIL — `camera/CameraModels.h` not found.

- [ ] **Step 4: Write the implementation**

Create `src/camera/CameraModels.h`:

```cpp
#pragma once

#include <string>
#include <vector>
#include <cctype>

// Pure, Qt-free catalog of camera bodies and their nominal AF coordinate frame
// sizes. The true AF frame is not exposed by libgphoto2, so these defaults are
// calibration starting points; the UI remembers per-model overrides.
namespace cammodel {

struct Model {
    const char *id;       // stable settings key, e.g. "d7500"
    const char *display;  // "Nikon D7500"
    int afFrameW;         // nominal default
    int afFrameH;
};

inline const std::vector<Model>& models() {
    static const std::vector<Model> kModels = {
        {"d7500", "Nikon D7500", 640, 426},
        {"d750",  "Nikon D750",  640, 426},
        {"d780",  "Nikon D780",  640, 426},
        {"d850",  "Nikon D850",  640, 426},
        {"d500",  "Nikon D500",  640, 426},
        {"d5600", "Nikon D5600", 640, 426},
        {"d3500", "Nikon D3500", 640, 426},
        {"z6",    "Nikon Z6 / Z6II", 640, 426},
        {"z7",    "Nikon Z7 / Z7II", 640, 426},
        {"custom", "Other / Custom", 640, 426},
    };
    return kModels;
}

inline const Model* byId(const std::string& id) {
    for (const Model& m : models())
        if (id == m.id) return &m;
    return nullptr;
}

// Case-insensitive substring match of each model's id token in the camera name.
// Longer ids are checked first so "d7500" wins over a hypothetical "d750"
// substring. "custom" is never auto-matched.
inline std::string matchModel(const std::string& cameraName) {
    std::string hay = cameraName;
    for (char& c : hay) c = char(std::tolower((unsigned char)c));

    const std::string* best = nullptr;
    static std::vector<std::string> ids;
    ids.clear();
    for (const Model& m : models()) {
        std::string id = m.id;
        if (id == "custom") continue;
        if (hay.find(id) != std::string::npos) {
            if (!best || id.size() > best->size()) {
                static std::string keep;
                keep = id;
                best = &keep;
            }
        }
    }
    return best ? *best : std::string();
}

} // namespace cammodel
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cmake --build build --target cam_models_test && ctest --test-dir build -R cam_models_test --output-on-failure
```
Expected: PASS — `CameraModelsTest: all assertions passed`.

- [ ] **Step 6: Commit**

```bash
git add src/camera/CameraModels.h tests/CameraModelsTest.cpp CMakeLists.txt
git commit -m "feat: add camera model catalog with name matcher and test"
```

---

### Task 2: PreferencesDialog

**Files:**
- Create: `src/ui/PreferencesDialog.h`
- Create: `src/ui/PreferencesDialog.cpp`
- Modify: `CMakeLists.txt` (add `src/ui/PreferencesDialog.cpp` to the `nikontether` target sources)

**Interfaces:**
- Consumes: `cammodel::models/byId` from Task 1.
- Produces:
  - `class PreferencesDialog : public QDialog` with:
    - `explicit PreferencesDialog(QWidget *parent = nullptr);`
    - `public slot void selectModelById(const QString& id);`
    - `signal void afFrameSizeChanged(int w, int h);`

- [ ] **Step 1: Create the header**

Create `src/ui/PreferencesDialog.h`:

```cpp
#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;

// File → Preferences… dialog. Holds the camera-model dropdown and the AF
// coordinate frame size used by click-to-focus. AF frame is remembered per
// model in QSettings; this dialog is the single writer.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

public slots:
    // Select a model by id (used for auto-detect). No-op if id is empty or
    // already the current selection, so a manual override survives reconnects.
    void selectModelById(const QString &id);

signals:
    void afFrameSizeChanged(int w, int h);

private:
    void onModelChanged();
    void onFrameEdited();
    void loadFrameForCurrentModel();
    QString currentModelId() const;

    QComboBox *m_model = nullptr;
    QSpinBox *m_frameW = nullptr;
    QSpinBox *m_frameH = nullptr;
};

// Returns the persisted AF frame for a model id (per-model override, else the
// model's built-in default, else 640x426). Shared by the dialog and startup
// seeding in RetouchWindow.
void afFrameForModel(const QString &id, int &w, int &h);
```

- [ ] **Step 2: Create the implementation**

Create `src/ui/PreferencesDialog.cpp`:

```cpp
#include "ui/PreferencesDialog.h"

#include "camera/CameraModels.h"

#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>

void afFrameForModel(const QString &id, int &w, int &h) {
    QSettings s;
    const cammodel::Model *m = cammodel::byId(id.toStdString());
    int dw = m ? m->afFrameW : 640;
    int dh = m ? m->afFrameH : 426;
    w = s.value(QString("af/models/%1/frameWidth").arg(id), dw).toInt();
    h = s.value(QString("af/models/%1/frameHeight").arg(id), dh).toInt();
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");

    auto *outer = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_model = new QComboBox;
    for (const cammodel::Model &m : cammodel::models())
        m_model->addItem(m.display, QString::fromLatin1(m.id));
    form->addRow("Camera model:", m_model);

    m_frameW = new QSpinBox;
    m_frameH = new QSpinBox;
    m_frameW->setRange(1, 20000);
    m_frameH->setRange(1, 20000);
    form->addRow("AF frame width:", m_frameW);
    form->addRow("AF frame height:", m_frameH);

    auto *hint = new QLabel(
        "Click-to-focus calibration. Center is always correct; tune the AF "
        "frame until edge clicks focus where the reticle is drawn. Values are "
        "remembered per model.");
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(buttons);

    // Restore the last-used model.
    QSettings s;
    QString cur = s.value("af/currentModel", "custom").toString();
    int idx = m_model->findData(cur);
    if (idx < 0) idx = m_model->findData("custom");
    {
        QSignalBlocker b(m_model);
        m_model->setCurrentIndex(idx);
    }
    loadFrameForCurrentModel();

    connect(m_model, &QComboBox::currentIndexChanged, this,
            [this](int) { onModelChanged(); });
    connect(m_frameW, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
    connect(m_frameH, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
}

QString PreferencesDialog::currentModelId() const {
    return m_model->currentData().toString();
}

void PreferencesDialog::loadFrameForCurrentModel() {
    int w = 0, h = 0;
    afFrameForModel(currentModelId(), w, h);
    QSignalBlocker bw(m_frameW);
    QSignalBlocker bh(m_frameH);
    m_frameW->setValue(w);
    m_frameH->setValue(h);
}

void PreferencesDialog::onModelChanged() {
    QSettings s;
    s.setValue("af/currentModel", currentModelId());
    loadFrameForCurrentModel();
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::onFrameEdited() {
    QSettings s;
    const QString id = currentModelId();
    s.setValue(QString("af/models/%1/frameWidth").arg(id), m_frameW->value());
    s.setValue(QString("af/models/%1/frameHeight").arg(id), m_frameH->value());
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::selectModelById(const QString &id) {
    if (id.isEmpty()) return;
    int idx = m_model->findData(id);
    if (idx < 0 || idx == m_model->currentIndex()) return;
    m_model->setCurrentIndex(idx); // triggers onModelChanged()
}
```

- [ ] **Step 3: Add to the CMake target**

In `CMakeLists.txt`, add to the `nikontether` source list (after `src/ui/ControlsPanel.cpp`):

```cmake
    src/ui/PreferencesDialog.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
cmake -S . -B build >/dev/null && cmake --build build --target nikontether
```
Expected: PASS — compiles and links (PreferencesDialog is built and moc'd).

- [ ] **Step 5: Commit**

```bash
git add src/ui/PreferencesDialog.h src/ui/PreferencesDialog.cpp CMakeLists.txt
git commit -m "feat: add Preferences dialog with per-model AF frame calibration"
```

---

### Task 3: Move AF fields out of ControlsPanel; add TetherView forwarder + cameraConnected

Revert the AF spin boxes from `ControlsPanel` (now in the dialog) and give
`TetherView` a `setAfFrameSize` forwarder plus a `cameraConnected` signal.

**Files:**
- Modify: `src/ui/ControlsPanel.h`
- Modify: `src/ui/ControlsPanel.cpp`
- Modify: `src/ui/TetherView.h`
- Modify: `src/ui/TetherView.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `void TetherView::setAfFrameSize(int w, int h);` (public)
  - `void TetherView::cameraConnected(const QString& cameraName);` (signal)

- [ ] **Step 1: Revert ControlsPanel header**

In `src/ui/ControlsPanel.h`, remove the `class QSpinBox;` forward declaration, the `void afFrameSizeChanged(int w, int h);` signal, and these private members:

```cpp
    QSpinBox *m_afFrameW = nullptr;
    QSpinBox *m_afFrameH = nullptr;
    void loadAfFrameSettings();
```

The class returns to its pre-base-plan shape (combos + AF/Capture buttons + `m_form`).

- [ ] **Step 2: Revert ControlsPanel implementation**

In `src/ui/ControlsPanel.cpp`:
- Remove the includes `#include <QSpinBox>` and `#include <QSettings>`.
- Remove the entire AF calibration block inserted after `outer->addStretch(1);`
  (the `afForm`/`m_afFrameW`/`m_afFrameH`/`persist`/`loadAfFrameSettings()` and the two spin-box `connect` calls), so `outer->addStretch(1);` is immediately followed by `m_afButton = new QPushButton("Autofocus");`.
- Remove the `emit afFrameSizeChanged(...)` line at the end of the constructor
  (leave `setEnabledControls(false);` as the last statement).
- Remove the whole `loadAfFrameSettings()` method definition.

Verify: `grep -n "afFrame\|QSpinBox\|loadAfFrameSettings" src/ui/ControlsPanel.cpp src/ui/ControlsPanel.h` returns nothing.

- [ ] **Step 3: TetherView header — forwarder + signal**

In `src/ui/TetherView.h`, add the public method (near `setActive`):

```cpp
    // Set the AF coordinate frame size used by click-to-focus.
    void setAfFrameSize(int w, int h);
```

And add to the `signals:` block (after `void statusMessage(...)`):

```cpp
    void cameraConnected(const QString &cameraName);
```

- [ ] **Step 4: TetherView implementation — forwarder, emit, remove old wiring**

In `src/ui/TetherView.cpp`:

Remove the base-plan block that connected ControlsPanel and seeded from the old global keys (lines that read):

```cpp
    connect(m_controls, &ControlsPanel::afFrameSizeChanged,
            m_liveView, &LiveViewWidget::setAfFrameSize);

    // Seed the live view with the persisted AF frame size.
    {
        QSettings s;
        m_liveView->setAfFrameSize(s.value("af/frameWidth", 640).toInt(),
                                   s.value("af/frameHeight", 426).toInt());
    }
```

Remove the now-unused `#include <QSettings>` (if no other use remains — verify with `grep -n QSettings src/ui/TetherView.cpp`).

Add the forwarder method (anywhere at file scope, e.g. after the constructor):

```cpp
void TetherView::setAfFrameSize(int w, int h) {
    m_liveView->setAfFrameSize(w, h);
}
```

In `handleConnected`, emit the new signal:

```cpp
void TetherView::handleConnected(const QString &name, const ConfigOptionMap &options) {
    m_controls->populate(options);
    setConnectedState(true);
    emit statusMessage("Connected: " + name);
    emit cameraConnected(name);
}
```

- [ ] **Step 5: Build to verify it compiles**

Run:
```bash
cmake --build build --target nikontether
```
Expected: PASS — no references to the removed `ControlsPanel::afFrameSizeChanged` remain.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ControlsPanel.h src/ui/ControlsPanel.cpp src/ui/TetherView.h src/ui/TetherView.cpp
git commit -m "refactor: move AF frame fields to Preferences; add TetherView forwarder"
```

---

### Task 4: RetouchWindow — Preferences menu + wiring + auto-detect

**Files:**
- Modify: `src/edit/RetouchWindow.h`
- Modify: `src/edit/RetouchWindow.cpp`

**Interfaces:**
- Consumes: `PreferencesDialog`, `TetherView::setAfFrameSize`, `TetherView::cameraConnected`, `cammodel::matchModel`, `afFrameForModel`.
- Produces: nothing (top-level wiring).

- [ ] **Step 1: Add the dialog member to the header**

In `src/edit/RetouchWindow.h`, add a forward declaration near `class TetherView;`:

```cpp
class PreferencesDialog;
```

And a private member near `m_tetherView`:

```cpp
    PreferencesDialog *m_prefsDialog = nullptr;
```

- [ ] **Step 2: Include headers in the implementation**

In `src/edit/RetouchWindow.cpp`, add near the other UI includes (with `#include "ui/TetherView.h"`):

```cpp
#include "ui/PreferencesDialog.h"
#include "camera/CameraModels.h"
```

And ensure `#include <QSettings>` is present (add if missing — verify with `grep -n "QSettings" src/edit/RetouchWindow.cpp`).

- [ ] **Step 3: Create the dialog and wire it after the tether view exists**

In `src/edit/RetouchWindow.cpp`, immediately after the tether status/message connections (after `RetouchWindow.cpp:360`, the `statusMessage` connect), add:

```cpp
    // Preferences dialog: per-model AF frame calibration for click-to-focus.
    m_prefsDialog = new PreferencesDialog(this);
    connect(m_prefsDialog, &PreferencesDialog::afFrameSizeChanged,
            m_tetherView, &TetherView::setAfFrameSize);
    connect(m_tetherView, &TetherView::cameraConnected, this,
            [this](const QString &name) {
                m_prefsDialog->selectModelById(
                    QString::fromStdString(cammodel::matchModel(name.toStdString())));
            });

    // Apply the current model's AF frame at startup.
    {
        QSettings s;
        const QString model = s.value("af/currentModel", "custom").toString();
        int w = 0, h = 0;
        afFrameForModel(model, w, h);
        m_tetherView->setAfFrameSize(w, h);
    }
```

- [ ] **Step 4: Add the File → Preferences… action**

In `src/edit/RetouchWindow.cpp`, in the File menu construction (after `m_fileMenu->addAction(m_exportAction);`, RetouchWindow.cpp:267), add:

```cpp
    m_fileMenu->addSeparator();
    QAction *prefsAction = new QAction("Preferences…", this);
    prefsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(prefsAction, &QAction::triggered, this, [this] {
        m_prefsDialog->show();
        m_prefsDialog->raise();
        m_prefsDialog->activateWindow();
    });
    m_fileMenu->addAction(prefsAction);
```

Note: `m_prefsDialog` must be constructed before this menu code runs. If the menu is built before the tether wiring in Step 3, move the `m_prefsDialog = new PreferencesDialog(this);` line (only the construction, not the connects) to just before the File menu block. Verify by reading the constructor order; the dialog pointer must be non-null when the File menu lambda is created (the lambda only dereferences it on trigger, so construction merely needs to happen before any Preferences… trigger — but construct it before Step 3's connects regardless).

- [ ] **Step 5: Build and smoke-test**

Run:
```bash
cmake --build build --target nikontether && ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 3 ./build/nikontether >/tmp/nt.log 2>&1; echo "exit=$? (124=ok)"
```
Expected: build PASS; all tests pass; app stays up (exit 124).

- [ ] **Step 6: Manual verification + commit**

Manual (GUI): File → Preferences… opens; switching models reloads the spin
boxes; editing then reopening shows the value persisted per model; connecting a
D7500 auto-selects "Nikon D7500"; the live-view reticle still focuses correctly
with the selected frame.

Commit:
```bash
git add src/edit/RetouchWindow.h src/edit/RetouchWindow.cpp
git commit -m "feat: add File > Preferences with camera model selection and auto-detect"
```

---

## Notes for the implementer

- Construction order in `RetouchWindow`: build `m_tetherView` first (it already
  exists at RetouchWindow.cpp:314), then `m_prefsDialog`, then wire, then the
  File menu action. The File menu is built earlier (line 257) than the tether
  view (line 314) in the current code — so construct `m_prefsDialog` right before
  the File menu block AND do the tether-dependent connects (Step 3) after line
  360 where `m_tetherView` exists. The Preferences… lambda only uses
  `m_prefsDialog`, which is safe as long as it is constructed before the lambda
  can fire (i.e. before the window is shown).
- `Qt::Key_Comma` with `Qt::CTRL` yields the standard Ctrl+, preferences shortcut.
- No `qRegisterMetaType` needed: `afFrameSizeChanged(int,int)` and
  `cameraConnected(QString)` use already-registered types and are direct
  (same-thread) connections.
