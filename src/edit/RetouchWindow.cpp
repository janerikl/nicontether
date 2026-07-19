#include "edit/RetouchWindow.h"
#include "edit/RetouchTab.h"
#include "edit/ExportDialog.h"
#include "edit/CurveEditor.h"
#include "edit/EditSidecar.h"
#include "edit/RecentSessions.h"
#include "ui/FilmstripWidget.h"
#include "ui/LevelsPanel.h"
#include "ui/MaskPanel.h"
#include "ui/ToolFlyout.h"
#include "ui/TetherView.h"
#include "ui/PreferencesDialog.h"
#include "camera/CameraModels.h"
#include "ui/ControlsPanel.h"
#include "capture/NefPreview.h"

#include <QScrollArea>
#include <QSettings>
#include <QCloseEvent>
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
#include <QActionGroup>
#include <QStatusBar>
#include <QFile>
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
// in this project). Each icon is drawn twice: a neutral dark-grey Off state
// and a light On state so the active (checked) tool stands out clearly.
constexpr int kIconPx = 28;
const QColor kIconOff(70, 70, 70);    // idle: dark grey
const QColor kIconOn(235, 235, 235);  // active: light

QPixmap drawZoom(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(c, 2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(4, 4, 14, 14));
    p.drawLine(QPointF(15, 15), QPointF(23, 23));
    return pm;
}

QPixmap drawCrop(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(c, 2);
    pen.setCapStyle(Qt::SquareCap);
    p.setPen(pen);
    // Two overlapping corner brackets, the classic crop-tool mark.
    p.drawLine(8, 4, 8, 20);
    p.drawLine(8, 20, 24, 20);
    p.drawLine(4, 8, 20, 8);
    p.drawLine(20, 8, 20, 24);
    return pm;
}

QPixmap drawHeal(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(c, 2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(4, 4, 20, 20)); // brush outline
    p.setPen(Qt::NoPen);
    QColor fill = c;
    fill.setAlpha(140);
    p.setBrush(fill);
    p.drawEllipse(QRectF(10, 10, 8, 8)); // spot being healed
    return pm;
}

QPixmap drawMask(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Half-filled circle: the classic "mask" motif.
    p.setPen(QPen(c, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(4, 4, 20, 20));
    p.setPen(Qt::NoPen);
    QColor fill = c;
    fill.setAlpha(140);
    p.setBrush(fill);
    p.drawPie(QRectF(4, 4, 20, 20), 90 * 16, 180 * 16);
    return pm;
}

// Mask subtool glyphs, used both on the flyout strip and (as the active
// subtool) on the mask tool button itself.
QPixmap drawMaskRadial(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(3, 6, 22, 16)); // outer ellipse
    QColor fill = c;
    fill.setAlpha(120);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(QRectF(9, 10, 10, 8)); // inner falloff
    return pm;
}

QPixmap drawMaskLinear(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Graduated: a band shaded on one side, split by the gradient line.
    QColor fill = c;
    fill.setAlpha(120);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRect(QRectF(4, 4, 20, 8));
    p.setPen(QPen(c, 2));
    p.drawLine(QPointF(4, 14), QPointF(24, 14));
    return pm;
}

QPixmap drawMaskBrush(const QColor &c) {
    QPixmap pm(kIconPx, kIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // A soft brush dab.
    QColor fill = c;
    fill.setAlpha(120);
    p.setPen(QPen(c, 2));
    p.setBrush(fill);
    p.drawEllipse(QRectF(6, 6, 16, 16));
    return pm;
}

// Overlay a small corner triangle marking a tool that owns a subtool flyout.
void addFlyoutMarker(QPixmap &pm, const QColor &c) {
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal s = kIconPx;
    QPolygonF tri;
    tri << QPointF(s - 6, s) << QPointF(s, s - 6) << QPointF(s, s);
    p.drawPolygon(tri);
}

// Build a two-state icon: dark grey when idle, light when checked/active.
QIcon makeToolIcon(QPixmap (*draw)(const QColor &)) {
    QIcon icon;
    icon.addPixmap(draw(kIconOff), QIcon::Normal, QIcon::Off);
    icon.addPixmap(draw(kIconOn), QIcon::Normal, QIcon::On);
    icon.addPixmap(draw(kIconOn), QIcon::Active, QIcon::On);
    return icon;
}

QIcon makeZoomIcon() { return makeToolIcon(drawZoom); }
QIcon makeCropIcon() { return makeToolIcon(drawCrop); }
QIcon makeHealIcon() { return makeToolIcon(drawHeal); }
QIcon makeMaskIcon() { return makeToolIcon(drawMask); }

// Two-state icon like makeToolIcon, but with the flyout corner marker baked in.
// Used for the mask tool button, whose glyph reflects its active subtool.
QIcon makeFlyoutToolIcon(QPixmap (*draw)(const QColor &)) {
    QIcon icon;
    QPixmap off = draw(kIconOff);
    addFlyoutMarker(off, kIconOff);
    QPixmap on = draw(kIconOn);
    addFlyoutMarker(on, kIconOn);
    icon.addPixmap(off, QIcon::Normal, QIcon::Off);
    icon.addPixmap(on, QIcon::Normal, QIcon::On);
    icon.addPixmap(on, QIcon::Active, QIcon::On);
    return icon;
}

// Glyph for a given mask subtype (shared by the flyout and the tool button).
QPixmap (*maskGlyph(MaskType t))(const QColor &) {
    switch (t) {
    case MaskType::Radial: return drawMaskRadial;
    case MaskType::Linear: return drawMaskLinear;
    case MaskType::Brush:  return drawMaskBrush;
    case MaskType::None:   return drawMask;
    }
    return drawMaskRadial;
}
} // namespace

RetouchWindow::RetouchWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("NikonTether");
    resize(1200, 820);

    auto *toolbar = addToolBar("Main");
    toolbar->setObjectName("mainToolBar");
    toolbar->setMovable(false);

    // Mode switch: mutually-exclusive Tether / Retouch at the far left.
    m_retouchModeAction = toolbar->addAction("Retouch");
    m_tetherModeAction = toolbar->addAction("Tether");
    m_retouchModeAction->setCheckable(true);
    m_tetherModeAction->setCheckable(true);
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(m_retouchModeAction);
    modeGroup->addAction(m_tetherModeAction);
    connect(m_retouchModeAction, &QAction::triggered, this,
            [this] { setMode(Mode::Retouch); });
    connect(m_tetherModeAction, &QAction::triggered, this,
            [this] { setMode(Mode::Tether); });
    // Save / Save All / Export live in the File menu only (not the toolbar).
    m_saveAction = new QAction("Save", this);
    m_saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    m_saveAllAction = new QAction("Save All", this);
    m_exportAction = new QAction("Export…", this);
    // Open Session/Photos moved out of the toolbar (still in the File menu)
    // to make room for the contextual tool-options row below.
    auto *openSessionAction = new QAction("Open Session…", this);
    auto *openPhotosAction = new QAction("Open Photos…", this);
    connect(openSessionAction, &QAction::triggered, this, &RetouchWindow::onOpenSession);
    connect(openPhotosAction, &QAction::triggered, this, &RetouchWindow::onOpenPhotos);
    connect(m_saveAction, &QAction::triggered, this, &RetouchWindow::onSave);
    connect(m_saveAllAction, &QAction::triggered, this, &RetouchWindow::onSaveAll);
    connect(m_exportAction, &QAction::triggered, this, &RetouchWindow::onExport);

    m_fileMenu = menuBar()->addMenu("File");
    m_fileMenu->addAction(openSessionAction);
    m_fileMenu->addAction(openPhotosAction);
    // Recent sessions live inline here, between these two separators. Recent
    // item actions are inserted before m_recentEndSeparator by
    // rebuildRecentSessionsMenu(); both separators hide when the list is empty.
    m_recentBeginSeparator = m_fileMenu->addSeparator();
    m_recentEndSeparator = m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_saveAction);
    m_fileMenu->addAction(m_saveAllAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exportAction);
    rebuildRecentSessionsMenu();

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

    editMenu->addSeparator();
    m_copyEditsAction = editMenu->addAction("Copy Edits");
    m_copyEditsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    m_pasteEditsAction = editMenu->addAction("Paste Edits");
    m_pasteEditsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    m_syncEditsAction = editMenu->addAction("Sync Edits to Selected");
    m_syncEditsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_copyEditsAction->setEnabled(false);
    m_pasteEditsAction->setEnabled(false);
    m_syncEditsAction->setEnabled(false);
    connect(m_copyEditsAction, &QAction::triggered, this, &RetouchWindow::onCopyEdits);
    connect(m_pasteEditsAction, &QAction::triggered, this, &RetouchWindow::onPasteEdits);
    connect(m_syncEditsAction, &QAction::triggered, this, &RetouchWindow::onSyncEdits);

    // Center: a stack (editing tabs / tether) with the shared filmstrip below,
    // so the filmstrip is visible in both modes.
    auto *central = new QWidget;
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &RetouchWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            &RetouchWindow::onTabCloseRequested);

    m_tetherView = new TetherView;

    m_modeStack = new QStackedWidget;
    m_modeStack->addWidget(m_tabs);       // index 0 = Retouch
    m_modeStack->addWidget(m_tetherView); // index 1 = Tether

    m_filmstrip = new FilmstripWidget;
    connect(m_filmstrip, &FilmstripWidget::frameSelected, this,
            &RetouchWindow::onFilmstripSelected);
    connect(m_filmstrip, &FilmstripWidget::retouchRequested, this,
            &RetouchWindow::onFilmstripSelected);
    connect(m_filmstrip, &FilmstripWidget::syncEditsRequested, this,
            &RetouchWindow::onSyncEdits);
    connect(m_filmstrip, &FilmstripWidget::deleteRequested, this,
            &RetouchWindow::onDeleteRequested);
    connect(m_filmstrip, &FilmstripWidget::itemSelectionChanged, this,
            &RetouchWindow::updateEditClipboardActions);

    vbox->addWidget(m_modeStack, 1);
    vbox->addWidget(m_filmstrip, 0);
    setCentralWidget(central);

    buildToolPanel();
    buildToolOptionsBar();
    buildDock();
    buildHistoryDock();
    buildLevelsDock();
    buildMaskDock();
    buildViewMenu();

    // Tether chrome: camera controls dock + tether action toolbar. Visibility is
    // driven by mode in Task 3; created hidden here.
    m_controlsDock = new QDockWidget("Controls", this);
    m_controlsDock->setObjectName("controlsDock");
    m_controlsDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_controlsDock->setWidget(m_tetherView->controlsPanel());
    addDockWidget(Qt::RightDockWidgetArea, m_controlsDock);
    m_controlsDock->hide();

    m_tetherToolBar = addToolBar("Tether");
    m_tetherToolBar->setObjectName("tetherToolBar");
    m_tetherToolBar->setMovable(false);
    m_tetherToolBar->addActions(m_tetherView->tetherActions());
    m_tetherToolBar->hide();

    // Captures flow into the shared filmstrip; tether status into the status bar.
    connect(m_tetherView, &TetherView::captureComplete, this,
            [this](const QString &path) { addToFilmstrip(path); });
    connect(m_tetherView, &TetherView::statusMessage, this,
            [this](const QString &msg) { m_statusLabel->setText(msg); });

    // Preferences dialog: per-model AF frame calibration for click-to-focus.
    m_prefsDialog = new PreferencesDialog(this);
    connect(m_prefsDialog, &PreferencesDialog::afFrameSizeChanged,
            m_tetherView, &TetherView::setAfFrameSize);
    connect(m_tetherView, &TetherView::cameraConnected, this,
            [this](const QString &name) {
                m_prefsDialog->selectModelById(
                    QString::fromStdString(cammodel::matchModel(name.toStdString())));
            });
    connect(m_prefsDialog, &PreferencesDialog::calibrationRequested, this,
            [this] {
                m_prefsDialog->hide();
                setMode(Mode::Tether);
                m_tetherView->startCalibration();
            });
    connect(m_tetherView, &TetherView::calibrationFinished, this,
            [this](int w, int h) {
                m_prefsDialog->setAfFrame(w, h);
                m_statusLabel->setText(
                    QString("Calibration saved: AF frame %1 × %2").arg(w).arg(h));
            });

    // Apply the current model's AF frame to the live view at startup.
    {
        QSettings s;
        const QString model = s.value("af/currentModel", "custom").toString();
        int w = 0, h = 0;
        afFrameForModel(model, w, h);
        m_tetherView->setAfFrameSize(w, h);
    }

    // File → Preferences…
    m_fileMenu->addSeparator();
    QAction *prefsAction = new QAction("Preferences…", this);
    prefsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(prefsAction, &QAction::triggered, this, [this] {
        m_prefsDialog->show();
        m_prefsDialog->raise();
        m_prefsDialog->activateWindow();
    });
    m_fileMenu->addAction(prefsAction);

    m_statusLabel = new QLabel("Open a photo to begin");
    statusBar()->addWidget(m_statusLabel);

    setDockEnabled(false);

    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WindowShortcut);
    connect(escShortcut, &QShortcut::activated, this, &RetouchWindow::deselectAllTools);

    // Ctrl+0 fits the image to the window, same as the Fit button.
    auto *fitShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    fitShortcut->setContext(Qt::WindowShortcut);
    connect(fitShortcut, &QShortcut::activated, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab && tab->isReady()) tab->zoomFit();
    });

    // Restore saved window geometry + dock layout, or fall back to defaults.
    // Restore runs *after* setMode so persisted show/hide of the editing docks
    // (including Layers) wins; then Controls visibility is re-asserted by app
    // logic.
    setMode(Mode::Retouch);
    QSettings settings;
    if (settings.contains("window/state")) {
        if (settings.contains("window/geometry"))
            restoreGeometry(settings.value("window/geometry").toByteArray());
        restoreState(settings.value("window/state").toByteArray());
        // Controls visibility is app-controlled, never persisted-visible.
        // The tether toolbar is likewise mode-driven: restoreState() would
        // resurrect it if the last session ended in Tether mode, but startup
        // always forces Retouch, so re-assert its hidden state here.
        if (m_controlsDock)   m_controlsDock->hide();
        if (m_tetherToolBar)  m_tetherToolBar->hide();
    } else {
        applyDefaultDockLayout();
    }
}

// View menu: toggle visibility of the Tools bar, Adjustments dock, and the
// filmstrip strip below the tabs. Built after those widgets exist.
void RetouchWindow::buildViewMenu() {
    auto *viewMenu = menuBar()->addMenu("View");
    if (m_toolsBar) viewMenu->addAction(m_toolsBar->toggleViewAction());
    if (m_adjustmentsDock) viewMenu->addAction(m_adjustmentsDock->toggleViewAction());
    if (m_historyDock) viewMenu->addAction(m_historyDock->toggleViewAction());
    if (m_levelsDock) viewMenu->addAction(m_levelsDock->toggleViewAction());
    if (m_maskDock) viewMenu->addAction(m_maskDock->toggleViewAction());

    auto *filmstripAction = new QAction("Filmstrip", this);
    filmstripAction->setCheckable(true);
    filmstripAction->setChecked(true);
    connect(filmstripAction, &QAction::toggled, this, [this](bool on) {
        if (m_filmstrip) m_filmstrip->setVisible(on);
    });
    viewMenu->addAction(filmstripAction);

    viewMenu->addSeparator();
    auto *resetPanelsAction = new QAction("Reset Panels", this);
    connect(resetPanelsAction, &QAction::triggered, this, [this] {
        QSettings settings;
        settings.remove("window/state"); // panels only; leave window/geometry
        applyDefaultDockLayout();
    });
    viewMenu->addAction(resetPanelsAction);
}

// Re-apply the default dock arrangement to the (already-created) docks: all
// editing docks on the right, Levels split above the Adjustments/History/Layers
// tab group, Tools toolbar on the left. applyModeChrome then asserts
// mode-driven visibility (editing docks hidden in Tether mode; Controls shown
// there instead). Used on first launch (no saved state) and by Reset Panels.
void RetouchWindow::applyDefaultDockLayout() {
    for (QDockWidget *d : {m_levelsDock, m_adjustmentsDock, m_historyDock,
                           m_maskDock, m_controlsDock}) {
        if (d) {
            d->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, d);
        }
    }
    if (m_adjustmentsDock && m_historyDock)
        tabifyDockWidget(m_adjustmentsDock, m_historyDock);
    if (m_adjustmentsDock && m_maskDock)
        tabifyDockWidget(m_adjustmentsDock, m_maskDock);
    if (m_levelsDock && m_adjustmentsDock)
        splitDockWidget(m_levelsDock, m_adjustmentsDock, Qt::Vertical);
    if (m_toolsBar)
        addToolBar(Qt::LeftToolBarArea, m_toolsBar);

    // Default visibility for the persistent editing docks.
    if (m_adjustmentsDock) m_adjustmentsDock->show();
    if (m_historyDock)     m_historyDock->show();
    if (m_levelsDock)      m_levelsDock->show();
    if (m_maskDock)        m_maskDock->show();

    // Let mode/tool chrome have the final say on editing-dock/Controls/Tools
    // visibility.
    const bool tether = m_tetherModeAction && m_tetherModeAction->isChecked();
    applyModeChrome(tether ? Mode::Tether : Mode::Retouch);
}

// Narrow left icon toolbar: mutually-exclusive Zoom / Crop / Spot Heal tools.
// Only one can be active; selecting one restricts the mouse gestures the
// canvas responds to (e.g. marquee-drag zoom and Ctrl+wheel only work while
// the Zoom tool is selected).
void RetouchWindow::buildToolPanel() {
    m_toolsBar = new QToolBar("Tools", this);
    m_toolsBar->setObjectName("toolsBar");
    m_toolsBar->setOrientation(Qt::Vertical);
    m_toolsBar->setIconSize(QSize(22, 22));
    // A dark, sunken background on the active tool so its light icon stands out.
    m_toolsBar->setStyleSheet(
        "QToolButton { border: none; padding: 4px; border-radius: 4px; }"
        "QToolButton:hover { background: rgba(0,0,0,0.08); }"
        "QToolButton:checked { background: #3a3f47; }");
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

    m_maskToggle = new FlyoutToolButton;
    m_maskToggle->setIcon(makeFlyoutToolIcon(maskGlyph(m_activeMaskSubtool)));
    m_maskToggle->setCheckable(true);
    m_maskToggle->setShortcut(QKeySequence(Qt::Key_K));
    m_maskToggle->setToolTip("Local Masks (K) — click to add; hold for radial / graduated / brush");
    m_toolsBar->addWidget(m_maskToggle);
    connect(m_maskToggle, &FlyoutToolButton::flyoutRequested, this,
            [this] { openMaskFlyout(); });

    // Each tool turns off the other two (and the WB eyedropper) when selected,
    // and swaps in that tool's options row under the main toolbar.
    connect(m_toolZoom, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            if (tab) { tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); tab->setMaskMode(false); }
            if (m_maskDock) m_maskDock->hide();
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
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setHealMode(false); tab->setWbPickMode(false); tab->setMaskMode(false); }
            if (m_maskDock) m_maskDock->hide();
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
            { QSignalBlocker b(m_maskToggle); m_maskToggle->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setWbPickMode(false); tab->setMaskMode(false); }
            if (m_maskDock) m_maskDock->hide();
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
    connect(m_maskToggle, &QToolButton::toggled, this, [this](bool on) {
        RetouchTab *tab = currentTab();
        if (on) {
            { QSignalBlocker b(m_toolZoom); m_toolZoom->setChecked(false); }
            { QSignalBlocker b(m_cropToggle); m_cropToggle->setChecked(false); }
            { QSignalBlocker b(m_healToggle); m_healToggle->setChecked(false); }
            { QSignalBlocker b(m_wbPick); m_wbPick->setChecked(false); }
            if (tab) { tab->setZoomMode(false); tab->setCropMode(false); tab->setHealMode(false); tab->setWbPickMode(false); }
            m_toolOptionsBar->setVisible(false); // layers use their own dock
            if (m_maskDock) { m_maskDock->show(); m_maskDock->raise(); }
        }
        // Layers panel stays visible when the K tool is toggled off — it's a
        // persistent stack, not a transient tool-options popup.
        if (tab && tab->isReady()) tab->setMaskMode(on);
        // A plain click on the tool creates a mask of the active subtool.
        if (on) addActiveMask();
        refreshMaskPanel();
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
    m_zoomSlider->setRange(10, 800);
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
    dock->setObjectName("adjustmentsDock");
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
    dock->setObjectName("historyDock");
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

void RetouchWindow::buildLevelsDock() {
    auto *dock = new QDockWidget("Levels", this);
    m_levelsDock = dock;
    dock->setObjectName("levelsDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_levelsPanel = new LevelsPanel;
    dock->setWidget(m_levelsPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    // Pin above the Adjustments dock so it reads at the top-right.
    if (m_adjustmentsDock)
        splitDockWidget(dock, m_adjustmentsDock, Qt::Vertical);

    connect(m_levelsPanel, &LevelsPanel::levelsChanged, this,
            [this](const Levels &lv) {
                if (m_syncing) return;
                RetouchTab *tab = currentTab();
                if (!tab || !tab->isReady()) return;
                Adjustments a = tab->adjustments();
                a.levels = lv;
                tab->setAdjustments(a);
            });
}

void RetouchWindow::refreshLevels() {
    if (!m_levelsPanel) return;
    RetouchTab *tab = currentTab();
    if (tab && tab->isReady()) {
        m_syncing = true;
        m_levelsPanel->setLevels(tab->adjustments().levels);
        m_syncing = false;
        if (!tab->previewImage().isNull())
            m_levelsPanel->setImage(tab->previewImage());
    } else {
        m_levelsPanel->clear();
    }
}

void RetouchWindow::buildMaskDock() {
    auto *dock = new QDockWidget("Layers", this);
    m_maskDock = dock;
    dock->setObjectName("maskDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_maskPanel = new MaskPanel;
    dock->setWidget(m_maskPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (m_adjustmentsDock) tabifyDockWidget(m_adjustmentsDock, dock);
    dock->hide(); // shown only while the Mask tool is active

    connect(m_maskPanel, &MaskPanel::selectMaskRequested, this, [this](int i) {
        RetouchTab *tab = currentTab();
        if (tab) tab->selectMask(i);
    });
    connect(m_maskPanel, &MaskPanel::deleteMaskRequested, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) { tab->deleteActiveMask(); refreshMaskPanel(); }
    });
    connect(m_maskPanel, &MaskPanel::duplicateMaskRequested, this, [this] {
        RetouchTab *tab = currentTab();
        if (tab) { tab->duplicateActiveMask(); refreshMaskPanel(); }
    });
    connect(m_maskPanel, &MaskPanel::maskAdjustChanged, this,
            [this](const MaskAdjust &a) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskAdjust(a);
            });
    connect(m_maskPanel, &MaskPanel::maskShapeChanged, this,
            [this](bool inv, double f, double h, double br, bool am) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskShape(inv, f, h, br, am);
            });
    connect(m_maskPanel, &MaskPanel::maskOpacityChanged, this,
            [this](double opacity) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskOpacity(opacity);
            });
    connect(m_maskPanel, &MaskPanel::maskBlendChanged, this,
            [this](BlendMode mode) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskBlend(mode);
            });
    connect(m_maskPanel, &MaskPanel::maskVisibleChanged, this,
            [this](bool visible) {
                RetouchTab *tab = currentTab();
                if (tab) { tab->setActiveMaskVisible(visible); refreshMaskPanel(); }
            });
    connect(m_maskPanel, &MaskPanel::maskNameChanged, this,
            [this](const QString &name) {
                RetouchTab *tab = currentTab();
                if (tab) tab->setActiveMaskName(name);
            });
    connect(m_maskPanel, &MaskPanel::maskReorderRequested, this,
            [this](int from, int to) {
                RetouchTab *tab = currentTab();
                if (tab) { tab->moveMask(from, to); refreshMaskPanel(); }
            });
}

void RetouchWindow::refreshMaskPanel() {
    if (!m_maskPanel) return;
    RetouchTab *tab = currentTab();
    if (tab && tab->isReady())
        m_maskPanel->setMasks(tab->masks(), tab->activeMaskIndex());
    else
        m_maskPanel->clear();
}

void RetouchWindow::openMaskFlyout() {
    const QVector<SubTool> tools{
        {int(MaskType::Radial), drawMaskRadial, "Radial", "Radial mask layer"},
        {int(MaskType::Linear), drawMaskLinear, "Graduated", "Graduated mask layer"},
        {int(MaskType::Brush), drawMaskBrush, "Brush", "Brush mask layer"},
        {int(MaskType::None), drawMask, "Layer", "Unmasked adjustment layer"},
    };
    auto *flyout = new ToolFlyout(tools, int(m_activeMaskSubtool), this);
    connect(flyout, &ToolFlyout::chosen, this, [this](int id) {
        setMaskSubtool(MaskType(id));
        // Picking a subtool activates the mask tool (creating a mask via the
        // toggle handler) or, if already active, creates one directly.
        if (m_maskToggle->isChecked())
            addActiveMask();
        else
            m_maskToggle->setChecked(true);
    });
    // Just to the right of the mask button, vertically aligned with it.
    const QPoint tl = m_maskToggle->mapToGlobal(QPoint(m_maskToggle->width() + 4, 0));
    flyout->showAt(tl);
}

void RetouchWindow::setMaskSubtool(MaskType t) {
    m_activeMaskSubtool = t;
    if (m_maskToggle)
        m_maskToggle->setIcon(makeFlyoutToolIcon(maskGlyph(t)));
}

void RetouchWindow::addActiveMask() {
    RetouchTab *tab = currentTab();
    if (tab && tab->isReady()) {
        tab->addMask(m_activeMaskSubtool);
        refreshMaskPanel();
    }
}

void RetouchWindow::mergePortable(const Adjustments &src, Adjustments &dst) {
    // Portable (image-independent) fields only; geometry & heals stay as dst had.
    dst.brightness = src.brightness;
    dst.contrast = src.contrast;
    dst.highlights = src.highlights;
    dst.shadows = src.shadows;
    dst.saturation = src.saturation;
    dst.vibrance = src.vibrance;
    dst.temperature = src.temperature;
    dst.tint = src.tint;
    dst.wbR = src.wbR;
    dst.wbG = src.wbG;
    dst.wbB = src.wbB;
    dst.clarity = src.clarity;
    dst.sharpen = src.sharpen;
    dst.vignette = src.vignette;
    dst.curve = src.curve;
    dst.levels = src.levels;
}

void RetouchWindow::updateEditClipboardActions() {
    RetouchTab *tab = currentTab();
    const bool tabReady = tab && tab->isReady();
    if (m_copyEditsAction) m_copyEditsAction->setEnabled(tabReady);
    if (m_pasteEditsAction)
        m_pasteEditsAction->setEnabled(m_hasEditClipboard && tabReady);
    if (m_syncEditsAction)
        m_syncEditsAction->setEnabled(m_hasEditClipboard &&
                                      !m_filmstrip->selectedPaths().isEmpty());
}

void RetouchWindow::onCopyEdits() {
    RetouchTab *tab = currentTab();
    if (!tab || !tab->isReady()) return;
    m_editClipboard = tab->adjustments();
    m_hasEditClipboard = true;
    updateEditClipboardActions();
    m_statusLabel->setText("Edits copied");
}

bool RetouchWindow::applyClipboardTo(const QString &path) {
    if (RetouchTab *tab = m_openTabs.value(path, nullptr)) {
        // Route through the open tab so history + dirty state stay consistent.
        Adjustments a = tab->adjustments();
        mergePortable(m_editClipboard, a);
        if (a == tab->adjustments()) return false;
        tab->setAdjustments(a);
        return true;
    }
    // Closed photo: merge into its sidecar (or defaults) and persist.
    Adjustments a;
    EditSidecar::load(path, a); // leaves a at defaults if none exists
    Adjustments before = a;
    mergePortable(m_editClipboard, a);
    if (a == before) return false;
    if (EditSidecar::save(path, a))
        m_filmstrip->setBadge(path, FilmstripWidget::Saved);
    return true;
}

void RetouchWindow::onPasteEdits() {
    RetouchTab *tab = currentTab();
    if (!m_hasEditClipboard || !tab || !tab->isReady()) return;
    applyClipboardTo(tab->path());
    m_statusLabel->setText("Edits pasted");
}

void RetouchWindow::onSyncEdits() {
    if (!m_hasEditClipboard) {
        m_statusLabel->setText("Copy edits first (Edit ▸ Copy Edits)");
        return;
    }
    const QStringList targets = m_filmstrip->selectedPaths();
    if (targets.isEmpty()) {
        m_statusLabel->setText("Select photos in the filmstrip to sync");
        return;
    }
    int changed = 0;
    for (const QString &path : targets)
        if (applyClipboardTo(path)) ++changed;
    m_statusLabel->setText(
        QString("Synced edits to %1 of %2 selected")
            .arg(changed)
            .arg(targets.size()));
}

RetouchTab *RetouchWindow::currentTab() const {
    return qobject_cast<RetouchTab *>(m_tabs->currentWidget());
}

void RetouchWindow::addToFilmstrip(const QString &path) {
    if (m_filmstripPaths.contains(path)) return;
    // Prefer the cached edited thumbnail so the strip shows the latest edits;
    // fall back to the NEF's embedded preview for un-edited photos.
    QImage thumb = EditSidecar::loadThumbnail(path);
    if (thumb.isNull())
        thumb = NefPreview::extract(path);
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
            refreshLevels();
            updateEditClipboardActions();
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
    connect(tab, &RetouchTab::maskBrushChanged, this, [this, tab](double radiusNorm) {
        if (tab != currentTab() || !m_maskPanel) return;
        m_maskPanel->setBrushRadius(radiusNorm); // reflect ctrl+wheel resize in the dock
    });
    connect(tab, &RetouchTab::previewUpdated, this, [this, tab] {
        if (tab == currentTab()) m_levelsPanel->setImage(tab->previewImage());
        // Reflect the edit live in the filmstrip thumbnail (in-memory; the
        // on-disk cache is written on save via EditSidecar::saveThumbnail).
        m_filmstrip->updateThumbnail(tab->path(), tab->previewImage());
    });
    connect(tab, &RetouchTab::masksChanged, this, [this, tab] {
        if (tab == currentTab()) refreshMaskPanel();
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
    setMode(Mode::Retouch);
    openPhoto(path);
}

void RetouchWindow::setMode(Mode mode) {
    m_modeStack->setCurrentWidget(mode == Mode::Tether
                                      ? static_cast<QWidget *>(m_tetherView)
                                      : static_cast<QWidget *>(m_tabs));
    applyModeChrome(mode);
    // Keep the toolbar buttons in sync when called programmatically.
    QSignalBlocker b1(m_tetherModeAction);
    QSignalBlocker b2(m_retouchModeAction);
    m_tetherModeAction->setChecked(mode == Mode::Tether);
    m_retouchModeAction->setChecked(mode == Mode::Retouch);
}

void RetouchWindow::closeEvent(QCloseEvent *event) {
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    QMainWindow::closeEvent(event);
}

void RetouchWindow::applyModeChrome(Mode mode) {
    const bool tether = (mode == Mode::Tether);

    // Tether chrome.
    if (m_tetherToolBar) m_tetherToolBar->setVisible(tether);
    if (m_controlsDock)  m_controlsDock->setVisible(tether);
    if (m_tetherView)    m_tetherView->setActive(tether);

    // Editing chrome.
    if (tether) deselectAllTools(); // exit any active tool + hide the options row
    if (m_toolsBar)        m_toolsBar->setVisible(!tether);
    if (m_adjustmentsDock) m_adjustmentsDock->setVisible(!tether);
    if (m_historyDock)     m_historyDock->setVisible(!tether);
    if (m_maskDock)        m_maskDock->setVisible(!tether);

    // Editing-only actions are meaningless while tethering.
    m_saveAction->setEnabled(!tether);
    m_saveAllAction->setEnabled(!tether);
    m_exportAction->setEnabled(!tether);
    if (tether) {
        m_undoAction->setEnabled(false);
        m_redoAction->setEnabled(false);
    } else {
        // Restore undo/redo + dock state for the current tab.
        onTabChanged(m_tabs->currentIndex());
    }
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
    if (m_maskToggle) {
        QSignalBlocker b(m_maskToggle);
        m_maskToggle->setChecked(false);
    }
    if (tab) tab->setMaskMode(false);
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
    refreshLevels();
    refreshMaskPanel();
    updateEditClipboardActions();
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

void RetouchWindow::onDeleteRequested(const QStringList &paths) {
    int deleted = 0, failed = 0;
    for (const QString &path : paths) {
        // Trash the RAW; skip UI/state removal if the file can't be trashed so
        // we never drop a thumbnail while its file remains on disk.
        if (!QFile::moveToTrash(path)) {
            ++failed;
            continue;
        }
        // Best-effort trash of the edit sidecar and cached thumbnail (may not exist).
        if (EditSidecar::exists(path))
            QFile::moveToTrash(EditSidecar::pathFor(path));
        QFile::moveToTrash(EditSidecar::thumbnailPathFor(path));

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
    if (!enabled && m_levelsPanel) m_levelsPanel->clear();
    if (!enabled && m_maskPanel) m_maskPanel->clear();
}

void RetouchWindow::onOpenSession() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Open session folder",
        QDir(QDir::homePath()).filePath("Pictures/Tether"));
    if (dir.isEmpty()) return;
    loadSession(dir);
}

// Scan a session folder for NEFs into the filmstrip, then record it as a
// recent session and refresh the File-menu section. Shared by onOpenSession()
// and the recent-entry click handler.
void RetouchWindow::loadSession(const QString &dir) {
    // A session replaces the filmstrip contents, so it shows only this
    // session's photos rather than appending to whatever was there before.
    m_filmstrip->clear();
    m_filmstripPaths.clear();

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

    RecentSessions::add(QDir(dir).absolutePath());
    rebuildRecentSessionsMenu();
}

// Repopulate the recent-session entries between m_recentBeginSeparator and
// m_recentEndSeparator, reflecting the current RecentSessions::load(). Both
// separators are hidden when the list is empty.
void RetouchWindow::rebuildRecentSessionsMenu() {
    if (!m_fileMenu) return;
    for (QAction *a : m_recentActions) {
        m_fileMenu->removeAction(a);
        a->deleteLater();
    }
    m_recentActions.clear();

    const QStringList recent = RecentSessions::load();
    for (const QString &dir : recent) {
        auto *act = new QAction(QDir(dir).dirName(), this);
        act->setToolTip(dir);
        connect(act, &QAction::triggered, this, [this, dir] {
            if (!QDir(dir).exists()) {
                QMessageBox::warning(
                    this, "Open Session",
                    "This session folder no longer exists:\n" + dir);
                RecentSessions::remove(dir);
                rebuildRecentSessionsMenu();
                return;
            }
            loadSession(dir);
        });
        // Insert before the closing separator so entries sit in the section.
        m_fileMenu->insertAction(m_recentEndSeparator, act);
        m_recentActions.append(act);
    }
    // Menu tooltips are not shown by default; enable them for path hints.
    m_fileMenu->setToolTipsVisible(true);

    const bool hasRecent = !m_recentActions.isEmpty();
    if (m_recentBeginSeparator) m_recentBeginSeparator->setVisible(hasRecent);
    if (m_recentEndSeparator) m_recentEndSeparator->setVisible(hasRecent);
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
