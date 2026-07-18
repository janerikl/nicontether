#include "edit/RetouchWindow.h"
#include "edit/RetouchTab.h"
#include "edit/ExportDialog.h"
#include "edit/CurveEditor.h"
#include "edit/EditSidecar.h"
#include "ui/FilmstripWidget.h"
#include "capture/NefPreview.h"

#include <QScrollArea>
#include <QKeySequence>
#include <cmath>

#include <QTabWidget>
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QSignalBlocker>

RetouchWindow::RetouchWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Retouch");
    resize(1200, 820);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    QAction *openSessionAction = toolbar->addAction("Open Session…");
    QAction *openPhotosAction = toolbar->addAction("Open Photos…");
    toolbar->addSeparator();
    QAction *saveAction = toolbar->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    QAction *saveAllAction = toolbar->addAction("Save All");
    toolbar->addSeparator();
    QAction *exportAction = toolbar->addAction("Export…");
    connect(openSessionAction, &QAction::triggered, this, &RetouchWindow::onOpenSession);
    connect(openPhotosAction, &QAction::triggered, this, &RetouchWindow::onOpenPhotos);
    connect(saveAction, &QAction::triggered, this, &RetouchWindow::onSave);
    connect(saveAllAction, &QAction::triggered, this, &RetouchWindow::onSaveAll);
    connect(exportAction, &QAction::triggered, this, &RetouchWindow::onExport);

    auto *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(openSessionAction);
    fileMenu->addAction(openPhotosAction);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAllAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction);

    // Center: tabs + filmstrip selector below.
    auto *central = new QWidget;
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &RetouchWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            &RetouchWindow::onTabCloseRequested);

    m_filmstrip = new FilmstripWidget;
    connect(m_filmstrip, &FilmstripWidget::frameSelected, this,
            &RetouchWindow::onFilmstripSelected);

    vbox->addWidget(m_tabs, 1);
    vbox->addWidget(m_filmstrip, 0);
    setCentralWidget(central);

    buildDock();

    m_statusLabel = new QLabel("Open a photo to begin");
    statusBar()->addWidget(m_statusLabel);

    setDockEnabled(false);
}

void RetouchWindow::buildDock() {
    auto *dock = new QDockWidget("Adjustments", this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    auto *panel = new QWidget;
    auto *outer = new QVBoxLayout(panel);

    auto makeSlider = [this](QFormLayout *form, const QString &label,
                             int lo = -100, int hi = 100) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(0);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, &RetouchWindow::onToneChanged);
        return s;
    };

    // Zoom row: Fit + slider + % (Ctrl+wheel and marquee-drag also zoom).
    outer->addWidget(new QLabel("<b>Zoom</b>"));
    auto *zoomRow = new QHBoxLayout;
    m_zoomFit = new QPushButton("Fit");
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setValue(100);
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(44);
    zoomRow->addWidget(m_zoomFit);
    zoomRow->addWidget(m_zoomSlider);
    zoomRow->addWidget(m_zoomLabel);
    outer->addLayout(zoomRow);
    connect(m_zoomFit, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->zoomFit();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_syncing) return;
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->setZoomPercent(v);
    });

    outer->addSpacing(6);

    // Press-and-hold to compare against the unedited image.
    m_beforeAfter = new QPushButton("Show Original (hold)");
    outer->addWidget(m_beforeAfter);
    connect(m_beforeAfter, &QPushButton::pressed, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->showOriginal(true);
    });
    connect(m_beforeAfter, &QPushButton::released, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->showOriginal(false);
    });

    outer->addWidget(new QLabel("<b>Tone</b>"));
    auto *toneForm = new QFormLayout;
    m_brightness = makeSlider(toneForm, "Brightness");
    m_contrast = makeSlider(toneForm, "Contrast");
    m_highlights = makeSlider(toneForm, "Highlights");
    m_shadows = makeSlider(toneForm, "Shadows");
    outer->addLayout(toneForm);

    outer->addSpacing(6);
    outer->addWidget(new QLabel("<b>Colour</b>"));
    auto *colForm = new QFormLayout;
    m_saturation = makeSlider(colForm, "Saturation");
    m_vibrance = makeSlider(colForm, "Vibrance");
    m_temperature = makeSlider(colForm, "Temperature");
    m_tint = makeSlider(colForm, "Tint (green/magenta)");
    outer->addLayout(colForm);
    m_wbPick = new QPushButton("White-balance eyedropper");
    m_wbPick->setCheckable(true);
    outer->addWidget(m_wbPick);
    connect(m_wbPick, &QPushButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setWbPickMode(on);
    });

    outer->addSpacing(6);
    outer->addWidget(new QLabel("<b>Tone Curve</b>"));
    m_curve = new CurveEditor;
    outer->addWidget(m_curve);
    connect(m_curve, &CurveEditor::curveChanged, this,
            [this](const QVector<QPointF> &pts) {
                if (m_syncing) return;
                RetouchTab *tab = currentTab();
                if (!tab || !tab->isReady()) return;
                Adjustments a = tab->adjustments();
                a.curve = pts;
                tab->setAdjustments(a);
            });

    outer->addSpacing(6);
    outer->addWidget(new QLabel("<b>Detail &amp; Effects</b>"));
    auto *fxForm = new QFormLayout;
    m_clarity = makeSlider(fxForm, "Clarity");
    m_sharpen = makeSlider(fxForm, "Sharpen", 0, 100);
    m_vignette = makeSlider(fxForm, "Vignette");
    outer->addLayout(fxForm);

    outer->addSpacing(8);
    outer->addWidget(new QLabel("<b>Orientation</b>"));
    auto *rotRow = new QHBoxLayout;
    m_rotLeft = new QPushButton("⟲ 90°");
    m_rotRight = new QPushButton("⟳ 90°");
    m_flipH = new QPushButton("Flip H");
    m_flipV = new QPushButton("Flip V");
    rotRow->addWidget(m_rotLeft);
    rotRow->addWidget(m_rotRight);
    outer->addLayout(rotRow);
    auto *flipRow = new QHBoxLayout;
    flipRow->addWidget(m_flipH);
    flipRow->addWidget(m_flipV);
    outer->addLayout(flipRow);

    // Orientation handlers mutate the current tab's adjustments.
    auto mutateCurrent = [this](std::function<void(Adjustments &)> fn) {
        RetouchTab *tab = currentTab();
        if (!tab || !tab->isReady()) return;
        Adjustments a = tab->adjustments();
        fn(a);
        tab->setAdjustments(a);
    };
    connect(m_rotLeft, &QPushButton::clicked, this, [mutateCurrent] {
        mutateCurrent([](Adjustments &a) { a.rotationQuadrants = (a.rotationQuadrants + 3) % 4; });
    });
    connect(m_rotRight, &QPushButton::clicked, this, [mutateCurrent] {
        mutateCurrent([](Adjustments &a) { a.rotationQuadrants = (a.rotationQuadrants + 1) % 4; });
    });
    connect(m_flipH, &QPushButton::clicked, this, [mutateCurrent] {
        mutateCurrent([](Adjustments &a) { a.flipH = !a.flipH; });
    });
    connect(m_flipV, &QPushButton::clicked, this, [mutateCurrent] {
        mutateCurrent([](Adjustments &a) { a.flipV = !a.flipV; });
    });

    outer->addSpacing(8);
    outer->addWidget(new QLabel("<b>Crop</b>"));

    // Aspect-ratio presets constrain the rubber-band drag (0 = freeform).
    m_cropAspect = new QComboBox;
    m_cropAspect->addItem("Freeform", 0.0);
    m_cropAspect->addItem("1:1 (square)", 1.0);
    m_cropAspect->addItem("3:2", 3.0 / 2.0);
    m_cropAspect->addItem("4:3", 4.0 / 3.0);
    m_cropAspect->addItem("5:4", 5.0 / 4.0);
    m_cropAspect->addItem("16:9", 16.0 / 9.0);
    m_cropAspect->addItem("2:3 (portrait)", 2.0 / 3.0);
    m_cropAspect->addItem("3:4 (portrait)", 3.0 / 4.0);
    m_cropAspect->addItem("9:16 (portrait)", 9.0 / 16.0);
    auto *aspectForm = new QFormLayout;
    aspectForm->addRow("Ratio:", m_cropAspect);
    outer->addLayout(aspectForm);
    connect(m_cropAspect, &QComboBox::currentIndexChanged, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->setCropAspect(m_cropAspect->currentData().toDouble());
    });

    m_cropToggle = new QPushButton("Crop");
    m_cropToggle->setCheckable(true);
    m_cropApply = new QPushButton("Apply Crop");
    m_cropReset = new QPushButton("Reset Crop");
    m_cropApply->setEnabled(false);
    outer->addWidget(m_cropToggle);
    auto *cropRow = new QHBoxLayout;
    cropRow->addWidget(m_cropApply);
    cropRow->addWidget(m_cropReset);
    outer->addLayout(cropRow);

    connect(m_cropToggle, &QPushButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) {
            tab->setCropMode(on);
            if (on) tab->setCropAspect(m_cropAspect->currentData().toDouble());
        }
    });
    connect(m_cropApply, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->applyCrop();
        m_cropToggle->setChecked(false);
    });
    connect(m_cropReset, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->resetCrop();
    });

    outer->addStretch(1);

    // Many controls now — make the dock scrollable.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel);
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

RetouchTab *RetouchWindow::currentTab() const {
    return qobject_cast<RetouchTab *>(m_tabs->currentWidget());
}

void RetouchWindow::addToFilmstrip(const QString &path) {
    if (m_filmstripPaths.contains(path)) return;
    QImage thumb = NefPreview::extract(path);
    m_filmstrip->addCapture(path, thumb);
    m_filmstripPaths.insert(path);
    // Show a "saved edits exist" badge if a sidecar is already on disk.
    if (EditSidecar::exists(path))
        m_filmstrip->setBadge(path, FilmstripWidget::Saved);
}

void RetouchWindow::openPhoto(const QString &path) {
    addToFilmstrip(path);

    if (m_openTabs.contains(path)) {
        m_tabs->setCurrentWidget(m_openTabs.value(path));
        return;
    }

    auto *tab = new RetouchTab(path);
    m_openTabs.insert(path, tab);
    int idx = m_tabs->addTab(tab, QFileInfo(path).fileName());
    m_tabs->setCurrentIndex(idx);
    m_statusLabel->setText("Decoding " + QFileInfo(path).fileName() + "…");

    connect(tab, &RetouchTab::decoded, this, [this, tab](bool ok) {
        if (tab == currentTab()) {
            setDockEnabled(ok);
            syncDockFromTab();
            m_statusLabel->setText(ok ? "Ready: " + QFileInfo(tab->path()).fileName()
                                      : "Failed to decode " + QFileInfo(tab->path()).fileName());
        }
    });
    connect(tab, &RetouchTab::cropPending, this, [this, tab](bool has) {
        if (tab == currentTab()) m_cropApply->setEnabled(has);
    });
    connect(tab, &RetouchTab::cropModeExited, this, [this, tab] {
        if (tab == currentTab()) {
            QSignalBlocker b(m_cropToggle);
            m_cropToggle->setChecked(false);
        }
    });
    connect(tab, &RetouchTab::wbPicked, this, [this, tab] {
        if (tab == currentTab()) {
            QSignalBlocker b(m_wbPick);
            m_wbPick->setChecked(false);
            tab->setWbPickMode(false);
            m_statusLabel->setText("White balance set");
        }
    });
    connect(tab, &RetouchTab::zoomChanged, this, [this, tab](double pct) {
        if (tab != currentTab()) return;
        QSignalBlocker b(m_zoomSlider);
        m_zoomSlider->setValue(int(std::lround(pct)));
        m_zoomLabel->setText(QString::number(int(std::lround(pct))) + "%");
    });
    connect(tab, &RetouchTab::editStateChanged, this,
            [this, tab](bool dirty, bool hasEdits) {
                FilmstripWidget::Badge b = dirty ? FilmstripWidget::Unsaved
                                                 : (hasEdits ? FilmstripWidget::Saved
                                                             : FilmstripWidget::NoBadge);
                m_filmstrip->setBadge(tab->path(), b);
            });
}

void RetouchWindow::onFilmstripSelected(const QString &path) {
    openPhoto(path);
}

void RetouchWindow::onTabChanged(int) {
    RetouchTab *tab = currentTab();
    bool ready = tab && tab->isReady();
    setDockEnabled(ready);
    if (m_cropToggle) {
        QSignalBlocker b(m_cropToggle);
        m_cropToggle->setChecked(false);
    }
    m_cropApply->setEnabled(false);
    if (m_wbPick) {
        QSignalBlocker b(m_wbPick);
        m_wbPick->setChecked(false);
    }
    if (tab) tab->setWbPickMode(false);
    syncDockFromTab();
    if (ready) {
        QSignalBlocker b(m_zoomSlider);
        int pct = int(std::lround(tab->zoomPercent()));
        m_zoomSlider->setValue(std::clamp(pct, m_zoomSlider->minimum(), m_zoomSlider->maximum()));
        m_zoomLabel->setText(QString::number(pct) + "%");
    }
    if (tab)
        m_statusLabel->setText(ready ? "Ready: " + QFileInfo(tab->path()).fileName()
                                     : "Decoding " + QFileInfo(tab->path()).fileName() + "…");
}

void RetouchWindow::onTabCloseRequested(int index) {
    auto *tab = qobject_cast<RetouchTab *>(m_tabs->widget(index));
    if (!tab) return;
    m_openTabs.remove(tab->path());
    m_tabs->removeTab(index);
    tab->deleteLater();
}

void RetouchWindow::syncDockFromTab() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    m_syncing = true;
    Adjustments a = tab->adjustments();
    auto set = [](QSlider *s, int v) { QSignalBlocker b(s); s->setValue(v); };
    set(m_brightness, a.brightness);
    set(m_contrast, a.contrast);
    set(m_highlights, a.highlights);
    set(m_shadows, a.shadows);
    set(m_saturation, a.saturation);
    set(m_vibrance, a.vibrance);
    set(m_temperature, a.temperature);
    set(m_tint, a.tint);
    set(m_clarity, a.clarity);
    set(m_sharpen, a.sharpen);
    set(m_vignette, a.vignette);
    m_curve->setCurve(a.curve);
    m_syncing = false;
}

void RetouchWindow::onToneChanged() {
    if (m_syncing) return;
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    Adjustments a = tab->adjustments();
    a.brightness = m_brightness->value();
    a.contrast = m_contrast->value();
    a.highlights = m_highlights->value();
    a.shadows = m_shadows->value();
    a.saturation = m_saturation->value();
    a.vibrance = m_vibrance->value();
    a.temperature = m_temperature->value();
    a.tint = m_tint->value();
    a.clarity = m_clarity->value();
    a.sharpen = m_sharpen->value();
    a.vignette = m_vignette->value();
    tab->setAdjustments(a);
}

void RetouchWindow::setDockEnabled(bool enabled) {
    const QList<QWidget *> widgets = {
        m_brightness, m_contrast, m_highlights, m_shadows, m_saturation,
        m_vibrance, m_temperature, m_tint, m_clarity, m_sharpen, m_vignette,
        m_curve, m_wbPick, m_beforeAfter,
        m_zoomSlider, m_zoomFit,
        m_rotLeft, m_rotRight, m_flipH, m_flipV,
        m_cropToggle, m_cropReset, m_cropAspect};
    for (QWidget *w : widgets)
        if (w) w->setEnabled(enabled);
    if (!enabled && m_cropApply) m_cropApply->setEnabled(false);
}

void RetouchWindow::onOpenSession() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Open session folder",
        QDir(QDir::homePath()).filePath("Pictures/Tether"));
    if (dir.isEmpty()) return;

    int count = 0;
    const QFileInfoList files =
        QDir(dir).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (fi.suffix().compare("nef", Qt::CaseInsensitive) == 0) {
            addToFilmstrip(fi.absoluteFilePath());
            ++count;
        }
    }
    m_statusLabel->setText(
        QString("Loaded %1 photo(s) from %2").arg(count).arg(dir));
}

void RetouchWindow::onOpenPhotos() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Open photos for editing",
        QDir(QDir::homePath()).filePath("Pictures/Tether"),
        "RAW images (*.nef *.NEF *.cr2 *.cr3 *.arw *.dng *.raf *.rw2 *.orf);;All files (*)");
    for (const QString &f : files)
        openPhoto(f);
}

void RetouchWindow::onSave() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    tab->saveEdits();
    m_statusLabel->setText("Saved edits: " + QFileInfo(tab->path()).fileName());
}

void RetouchWindow::onSaveAll() {
    int n = 0;
    for (RetouchTab *tab : m_openTabs) {
        if (tab && tab->isReady() && tab->isDirty()) { tab->saveEdits(); ++n; }
    }
    m_statusLabel->setText(QString("Saved edits for %1 photo(s)").arg(n));
}

void RetouchWindow::onExport() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) {
        QMessageBox::information(this, "Export", "No decoded photo to export.");
        return;
    }

    ExportDialog dlg(&m_presetStore, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ExportPreset preset = dlg.selectedPreset();

    QImage rendered = tab->renderFullRes();
    if (rendered.isNull()) {
        QMessageBox::warning(this, "Export", "Nothing to export.");
        return;
    }
    QImage out = applyExportResize(rendered, preset);

    QFileInfo src(tab->path());
    QDir editedDir(src.absolutePath() + "/edited");
    editedDir.mkpath(".");
    QString suggested =
        editedDir.filePath(src.completeBaseName() + "." + preset.extension());
    QString filter = preset.format == ExportPreset::PNG ? "PNG (*.png)"
                                                        : "JPEG (*.jpg *.jpeg)";

    QString file = QFileDialog::getSaveFileName(this, "Export image", suggested, filter);
    if (file.isEmpty()) return;

    bool ok = preset.format == ExportPreset::PNG
                  ? out.save(file, "PNG")
                  : out.save(file, "JPEG", preset.quality);
    if (ok)
        m_statusLabel->setText(QString("Exported %1×%2 → %3")
                                   .arg(out.width()).arg(out.height()).arg(file));
    else
        QMessageBox::warning(this, "Export", "Failed to write " + file);
}
