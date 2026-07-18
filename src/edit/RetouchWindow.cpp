#include "edit/RetouchWindow.h"
#include "edit/RetouchTab.h"
#include "edit/ExportDialog.h"
#include "edit/CurveEditor.h"
#include "edit/EditSidecar.h"
#include "ui/FilmstripWidget.h"
#include "capture/NefPreview.h"

#include <QScrollArea>
#include <QKeySequence>
#include <QShortcut>
#include <cmath>

#include <QTabWidget>
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QLabel>
#include <QToolBar>
#include <QStackedWidget>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QSignalBlocker>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace {
// Small programmatically-drawn icons for the left tool bar (no image assets
// in this project). Neutral dark-grey strokes on a transparent background.
constexpr int kIconPx = 28;

QIcon makeZoomIcon() {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(70, 70, 70), 2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(4, 4, 14, 14));
    p.drawLine(QPointF(15, 15), QPointF(23, 23));
    return QIcon(pm);
}

QIcon makeCropIcon() {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(70, 70, 70), 2);
    pen.setCapStyle(Qt::SquareCap);
    p.setPen(pen);
    // Two overlapping corner brackets, the classic crop-tool mark.
    p.drawLine(8, 4, 8, 20);
    p.drawLine(8, 20, 24, 20);
    p.drawLine(4, 8, 20, 8);
    p.drawLine(20, 8, 20, 24);
    return QIcon(pm);
}

QIcon makeHealIcon() {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(70, 70, 70), 2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(4, 4, 20, 20)); // brush outline
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(70, 70, 70, 140));
    p.drawEllipse(QRectF(10, 10, 8, 8)); // spot being healed
    return QIcon(pm);
}
} // namespace

RetouchWindow::RetouchWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Retouch");
    resize(1200, 820);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    QAction *saveAction = toolbar->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    QAction *saveAllAction = toolbar->addAction("Save All");
    toolbar->addSeparator();
    QAction *exportAction = toolbar->addAction("Export…");
    // Open Session/Photos moved out of the toolbar (still in the File menu)
    // to make room for the contextual tool-options row below.
    auto *openSessionAction = new QAction("Open Session…", this);
    auto *openPhotosAction = new QAction("Open Photos…", this);
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

    auto *editMenu = menuBar()->addMenu("Edit");
    m_undoAction = editMenu->addAction("Undo");
    m_undoAction->setShortcut(QKeySequence::Undo); // Ctrl+Z
    m_redoAction = editMenu->addAction("Redo");
    m_redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y)); // Ctrl+Y
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->undo();
    });
    connect(m_redoAction, &QAction::triggered, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->redo();
    });

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

    buildToolPanel();
    buildToolOptionsBar();
    buildDock();
    buildHistoryDock();
    buildViewMenu();

    m_statusLabel = new QLabel("Open a photo to begin");
    statusBar()->addWidget(m_statusLabel);

    setDockEnabled(false);

    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WindowShortcut);
    connect(escShortcut, &QShortcut::activated, this, &RetouchWindow::deselectAllTools);
}

// View menu: toggle visibility of the Tools bar, Adjustments dock, and the
// filmstrip strip below the tabs. Built after those widgets exist.
void RetouchWindow::buildViewMenu() {
    auto *viewMenu = menuBar()->addMenu("View");
    if (m_toolsBar) viewMenu->addAction(m_toolsBar->toggleViewAction());
    if (m_adjustmentsDock) viewMenu->addAction(m_adjustmentsDock->toggleViewAction());
    if (m_historyDock) viewMenu->addAction(m_historyDock->toggleViewAction());

    auto *filmstripAction = new QAction("Filmstrip", this);
    filmstripAction->setCheckable(true);
    filmstripAction->setChecked(true);
    connect(filmstripAction, &QAction::toggled, this, [this](bool on) {
        if (m_filmstrip) m_filmstrip->setVisible(on);
    });
    viewMenu->addAction(filmstripAction);
}

// Narrow left icon toolbar: mutually-exclusive Zoom / Crop / Spot Heal tools.
// Only one can be active; selecting one restricts the mouse gestures the
// canvas responds to (e.g. marquee-drag zoom and Ctrl+wheel only work while
// the Zoom tool is selected).
void RetouchWindow::buildToolPanel() {
    m_toolsBar = new QToolBar("Tools", this);
    m_toolsBar->setOrientation(Qt::Vertical);
    m_toolsBar->setIconSize(QSize(22, 22));
    addToolBar(Qt::LeftToolBarArea, m_toolsBar);

    m_toolZoom = new QToolButton;
    m_toolZoom->setIcon(makeZoomIcon());
    m_toolZoom->setCheckable(true);
    m_toolZoom->setShortcut(QKeySequence(Qt::Key_Z));
    m_toolZoom->setToolTip("Zoom (Z) — drag to marquee-zoom, Ctrl+wheel to zoom");
    m_toolsBar->addWidget(m_toolZoom);

    m_cropToggle = new QToolButton;
    m_cropToggle->setIcon(makeCropIcon());
    m_cropToggle->setCheckable(true);
    m_cropToggle->setShortcut(QKeySequence(Qt::Key_C));
    m_cropToggle->setToolTip("Crop (C)");
    m_toolsBar->addWidget(m_cropToggle);

    m_healToggle = new QToolButton;
    m_healToggle->setIcon(makeHealIcon());
    m_healToggle->setCheckable(true);
    m_healToggle->setShortcut(QKeySequence(Qt::Key_H));
    m_healToggle->setToolTip("Spot Heal (H) — click blemishes; Ctrl+wheel resizes brush");
    m_toolsBar->addWidget(m_healToggle);

    // Each tool turns off the other two (and the WB eyedropper) when selected,
    // and swaps in that tool's options row under the main toolbar.
    connect(m_toolZoom, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            if (tab) { tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); }
            m_toolOptionsStack->setCurrentIndex(0);
            m_toolOptionsBar->setVisible(true);
        } else {
            m_toolOptionsBar->setVisible(false);
        }
        if (tab && tab->isReady()) tab->setZoomMode(on);
    });
    connect(m_cropToggle, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setHealMode(false); tab->setWbPickMode(false); }
            m_toolOptionsStack->setCurrentIndex(1);
            m_toolOptionsBar->setVisible(true);
        } else {
            m_toolOptionsBar->setVisible(false);
        }
        if (tab && tab->isReady()) {
            tab->setCropMode(on);
            if (on) tab->setCropAspect(m_cropAspect->currentData().toDouble());
        }
    });
    connect(m_healToggle, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setWbPickMode(false); }
            m_toolOptionsStack->setCurrentIndex(2);
            m_toolOptionsBar->setVisible(true);
        } else {
            m_toolOptionsBar->setVisible(false);
        }
        if (tab && tab->isReady()) {
            tab->setHealBrush(m_healBrush->value());
            tab->setHealMode(on);
        }
    });
}

// Contextual options row shown under the main toolbar only while a left-bar
// tool (Zoom/Crop/Heal) is selected; each tool gets its own page in the stack.
void RetouchWindow::buildToolOptionsBar() {
    addToolBarBreak(Qt::TopToolBarArea);
    m_toolOptionsBar = new QToolBar("Tool Options", this);
    m_toolOptionsBar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, m_toolOptionsBar);
    m_toolOptionsBar->setVisible(false); // hidden until a tool is selected

    m_toolOptionsStack = new QStackedWidget;

    // --- Zoom page (index 0) ---
    auto *zoomPage = new QWidget;
    auto *zoomRow = new QHBoxLayout(zoomPage);
    zoomRow->setContentsMargins(4, 2, 4, 2);
    m_zoomFit = new QPushButton("Fit");
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setMinimumWidth(160);
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(44);
    zoomRow->addWidget(new QLabel("Zoom:"));
    zoomRow->addWidget(m_zoomFit);
    zoomRow->addWidget(m_zoomSlider);
    zoomRow->addWidget(m_zoomLabel);
    zoomRow->addStretch(1);
    m_toolOptionsStack->addWidget(zoomPage);

    connect(m_zoomFit, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->zoomFit();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_syncing) return;
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->setZoomPercent(v);
    });

    // --- Crop page (index 1) ---
    auto *cropPage = new QWidget;
    auto *cropRow = new QHBoxLayout(cropPage);
    cropRow->setContentsMargins(4, 2, 4, 2);
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
    m_cropApply = new QPushButton("Apply Crop");
    m_cropReset = new QPushButton("Reset Crop");
    m_cropApply->setEnabled(false);
    cropRow->addWidget(new QLabel("Ratio:"));
    cropRow->addWidget(m_cropAspect);
    cropRow->addWidget(m_cropApply);
    cropRow->addWidget(m_cropReset);
    cropRow->addStretch(1);
    m_toolOptionsStack->addWidget(cropPage);

    connect(m_cropAspect, &QComboBox::currentIndexChanged, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->setCropAspect(m_cropAspect->currentData().toDouble());
    });
    connect(m_cropApply, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->applyCrop();
        m_cropToggle->setChecked(false); // also hides this options row
    });
    connect(m_cropReset, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->resetCrop();
    });

    // --- Spot Heal page (index 2) ---
    auto *healPage = new QWidget;
    auto *healRow = new QHBoxLayout(healPage);
    healRow->setContentsMargins(4, 2, 4, 2);
    m_healBrush = new QSlider(Qt::Horizontal);
    m_healBrush->setRange(4, 80);
    m_healBrush->setValue(20);
    m_healBrush->setMinimumWidth(160);
    m_healClear = new QPushButton("Clear Spots");
    healRow->addWidget(new QLabel("Brush size:"));
    healRow->addWidget(m_healBrush);
    healRow->addWidget(m_healClear);
    healRow->addStretch(1);
    m_toolOptionsStack->addWidget(healPage);

    connect(m_healBrush, &QSlider::valueChanged, this, [this](int v) {
        RetouchTab *tab = currentTab();
        if (tab) tab->setHealBrush(v);
    });
    connect(m_healClear, &QPushButton::clicked, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) tab->clearHeals();
    });

    m_toolOptionsBar->addWidget(m_toolOptionsStack);
}

void RetouchWindow::buildDock() {
    auto *dock = new QDockWidget("Adjustments", this);
    m_adjustmentsDock = dock;
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
        if (on) {
            // Mutually exclusive with the left-bar tools (Zoom/Crop/Heal).
            if (m_toolZoom) { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            if (m_cropToggle) { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            if (m_healToggle) { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setHealMode(false); }
        }
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

    outer->addStretch(1);

    // Many controls now — make the dock scrollable.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel);
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void RetouchWindow::buildHistoryDock() {
    auto *dock = new QDockWidget("History", this);
    m_historyDock = dock;
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_historyList = new QListWidget;
    m_historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_historyList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                RetouchTab *tab = currentTab();
                if (tab) tab->jumpToHistory(m_historyList->row(item));
            });
    dock->setWidget(m_historyList);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    // Stack under the Adjustments dock as a tab if both are on the right.
    if (m_adjustmentsDock) tabifyDockWidget(m_adjustmentsDock, dock);
}

void RetouchWindow::refreshHistoryPanel() {
    if (!m_historyList) return;
    RetouchTab *tab = currentTab();
    QSignalBlocker block(m_historyList);
    m_historyList->clear();
    if (!tab || !tab->isReady()) return;
    const QVector<Adjustments> &hist = tab->history();
    for (int i = 0; i < hist.size(); ++i) {
        QString label = (i == 0) ? QStringLiteral("Original")
                                 : historyStepLabel(hist[i - 1], hist[i]);
        m_historyList->addItem(label);
    }
    int cur = tab->historyIndex();
    if (cur >= 0 && cur < m_historyList->count())
        m_historyList->setCurrentRow(cur);
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
            refreshHistoryPanel();
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
            m_toolOptionsBar->setVisible(false);
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
    connect(tab, &RetouchTab::historyChanged, this,
            [this, tab](bool canUndo, bool canRedo) {
                if (tab != currentTab()) return;
                m_undoAction->setEnabled(canUndo);
                m_redoAction->setEnabled(canRedo);
            });
    connect(tab, &RetouchTab::historyListChanged, this, [this, tab] {
        if (tab != currentTab()) return;
        refreshHistoryPanel();
    });
    connect(tab, &RetouchTab::adjustmentsReplaced, this, [this, tab] {
        if (tab != currentTab()) return;
        syncDockFromTab(); // reflect undone/redone values in the dock
    });
    connect(tab, &RetouchTab::zoomChanged, this, [this, tab](double pct) {
        if (tab != currentTab()) return;
        QSignalBlocker b(m_zoomSlider);
        m_zoomSlider->setValue(int(std::lround(pct)));
        m_zoomLabel->setText(QString::number(int(std::lround(pct))) + "%");
    });
    connect(tab, &RetouchTab::healBrushChanged, this, [this, tab](int radius) {
        if (tab != currentTab()) return;
        QSignalBlocker b(m_healBrush);
        m_healBrush->setValue(radius); // reflect ctrl+wheel resize in the dock
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

void RetouchWindow::onTabChanged(int) {
    RetouchTab *tab = currentTab();
    bool ready = tab && tab->isReady();
    setDockEnabled(ready);
    deselectAllTools();
    m_undoAction->setEnabled(tab && tab->canUndo());
    m_redoAction->setEnabled(tab && tab->canRedo());
    syncDockFromTab();
    refreshHistoryPanel();
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
        m_zoomSlider, m_zoomFit, m_toolZoom,
        m_rotLeft, m_rotRight, m_flipH, m_flipV,
        m_cropToggle, m_cropReset, m_cropAspect,
        m_healToggle, m_healBrush, m_healClear};
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
