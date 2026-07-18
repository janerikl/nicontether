#include "ui/MainWindow.h"

#include "camera/CameraController.h"
#include "ui/LiveViewWidget.h"
#include "ui/ControlsPanel.h"
#include "ui/FilmstripWidget.h"
#include "ui/PreviewWindow.h"
#include "edit/RetouchWindow.h"
#include "capture/NefPreview.h"

#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QTabWidget>
#include <QStatusBar>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_controller = new CameraController(this);
    buildUi();

    connect(m_controller, &CameraController::connected, this, &MainWindow::handleConnected);
    connect(m_controller, &CameraController::disconnected, this, &MainWindow::handleDisconnected);
    connect(m_controller, &CameraController::liveFrame,
            m_liveView, &LiveViewWidget::setFrame);
    connect(m_controller, &CameraController::captureComplete,
            this, &MainWindow::handleCaptureComplete);
    connect(m_controller, &CameraController::captureStarted, this,
            [this] { m_statusLabel->setText("Capturing…"); });
    connect(m_controller, &CameraController::configChanged, this,
            [this](const QString &w, const QString &v) {
                m_controls->updateValue(w, v);
                m_statusLabel->setText(QString("Set %1 = %2").arg(w, v));
            });
    connect(m_controller, &CameraController::cameraError, this, &MainWindow::handleError);
    connect(m_controller, &CameraController::log, this,
            [this](const QString &m) { m_statusLabel->setText(m); });

    // Controls -> controller
    connect(m_controls, &ControlsPanel::configEditRequested,
            m_controller, &CameraController::setConfig);
    connect(m_controls, &ControlsPanel::autofocusRequested,
            m_controller, &CameraController::triggerAutofocus);
    connect(m_controls, &ControlsPanel::captureRequested,
            m_controller, &CameraController::capture);
    connect(m_liveView, &LiveViewWidget::focusRequested,
            m_controller, &CameraController::setAfArea);
    connect(m_filmstrip, &FilmstripWidget::frameSelected,
            this, &MainWindow::showPreviewFor);
    connect(m_filmstrip, &FilmstripWidget::retouchRequested,
            this, &MainWindow::openInRetouch);

    // Start with a default session so captures always have a home.
    m_controller->setSaveDirectory(m_session.startSession("studio"));
    setConnectedState(false);
}

void MainWindow::buildUi() {
    setWindowTitle("NikonTether");
    resize(1280, 800);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    m_connectAction = toolbar->addAction("Connect");
    m_disconnectAction = toolbar->addAction("Disconnect");
    toolbar->addSeparator();
    m_liveViewAction = toolbar->addAction("Live View");
    m_liveViewAction->setCheckable(true);
    toolbar->addSeparator();
    m_captureAction = toolbar->addAction("Capture (Space)");
    toolbar->addSeparator();
    m_sessionAction = toolbar->addAction("New Session…");

    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnect);
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::onDisconnect);
    connect(m_liveViewAction, &QAction::toggled, this, &MainWindow::onToggleLiveView);
    connect(m_captureAction, &QAction::triggered, m_controller, &CameraController::capture);
    connect(m_sessionAction, &QAction::triggered, this, &MainWindow::onNewSession);

    // Spacebar triggers a capture from anywhere in the window — except on the
    // Preview tab, where Space is reserved for pan (see the tab handler below).
    m_captureShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_captureShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_captureShortcut, &QShortcut::activated,
            m_controller, &CameraController::capture);

    // Center: tabbed Live View / Preview, with the filmstrip below.
    auto *central = new QWidget;
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_viewTabs = new QTabWidget;
    m_liveView = new LiveViewWidget;
    m_preview = new PreviewWindow;
    m_viewTabs->addTab(m_liveView, "Live View");
    m_viewTabs->addTab(m_preview, "Preview");

    m_filmstrip = new FilmstripWidget;
    vbox->addWidget(m_viewTabs, 1);
    vbox->addWidget(m_filmstrip, 0);
    setCentralWidget(central);

    // On the Preview tab, hand Space to the pan tool; elsewhere it captures.
    connect(m_viewTabs, &QTabWidget::currentChanged, this, [this](int) {
        bool onPreview = m_viewTabs->currentWidget() == m_preview;
        if (m_captureShortcut) m_captureShortcut->setEnabled(!onPreview);
        if (onPreview) m_preview->focusView();
    });

    // Right dock: controls.
    m_controlsDock = new QDockWidget("Controls", this);
    m_controlsDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_controls = new ControlsPanel;
    m_controlsDock->setWidget(m_controls);
    addDockWidget(Qt::RightDockWidgetArea, m_controlsDock);

    // Preview is embedded as a tab (created above), not a separate window.

    // View menu: toggle the controls dock back on after it's closed, and open
    // the retouch window.
    auto *viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(m_controlsDock->toggleViewAction());
    viewMenu->addSeparator();
    QAction *retouchAction = viewMenu->addAction("Retouch…");
    connect(retouchAction, &QAction::triggered, this,
            [this] { openInRetouch(QString()); });

    m_statusLabel = new QLabel("Disconnected");
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::onConnect() {
    m_statusLabel->setText("Connecting…");
    m_controller->connectCamera();
}

void MainWindow::onDisconnect() {
    if (m_liveViewAction->isChecked()) m_liveViewAction->setChecked(false);
    m_controller->disconnectCamera();
}

void MainWindow::onToggleLiveView(bool on) {
    if (on) m_controller->startLiveView();
    else { m_controller->stopLiveView(); m_liveView->clearFrame(); }
}

void MainWindow::onNewSession() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Session",
                                         "Session name:", QLineEdit::Normal,
                                         "studio", &ok);
    if (!ok) return;
    QString dir = m_session.startSession(name);
    m_controller->setSaveDirectory(dir);
    m_statusLabel->setText("Session: " + dir);
}

void MainWindow::handleConnected(const QString &name, const ConfigOptionMap &options) {
    m_controls->populate(options);
    setConnectedState(true);
    m_statusLabel->setText("Connected: " + name);
}

void MainWindow::handleDisconnected() {
    setConnectedState(false);
    m_liveView->clearFrame();
    m_statusLabel->setText("Disconnected");
}

void MainWindow::handleCaptureComplete(const QString &path) {
    QImage preview = NefPreview::extract(path);
    // Always record the capture in the filmstrip, even if no preview could be
    // extracted — the item then shows a placeholder rather than disappearing.
    m_filmstrip->addCapture(path, preview);
    m_captures << path;
    if (m_retouchWindow) m_retouchWindow->addToFilmstrip(path);
    if (!preview.isNull()) {
        m_preview->showImage(path, preview);
        m_viewTabs->setCurrentWidget(m_preview);
        m_statusLabel->setText("Captured: " + path);
    } else {
        m_statusLabel->setText("Captured (no embedded preview): " + path);
    }
}

void MainWindow::openInRetouch(const QString &path) {
    if (!m_retouchWindow) {
        m_retouchWindow = new RetouchWindow(this);
        m_retouchWindow->setAttribute(Qt::WA_QuitOnClose, false);
        for (const QString &p : m_captures)
            m_retouchWindow->addToFilmstrip(p);
    }
    m_retouchWindow->show();
    m_retouchWindow->raise();
    m_retouchWindow->activateWindow();

    QString target = path;
    if (target.isEmpty() && !m_captures.isEmpty())
        target = m_captures.last(); // most recent capture
    if (!target.isEmpty())
        m_retouchWindow->openPhoto(target);
}

void MainWindow::showPreviewFor(const QString &path) {
    QImage preview = NefPreview::extract(path);
    m_preview->showImage(path, preview);
    m_viewTabs->setCurrentWidget(m_preview);
}

void MainWindow::handleError(const QString &message) {
    m_statusLabel->setText("Error: " + message);
    QMessageBox::warning(this, "Camera Error", message);
}

void MainWindow::setConnectedState(bool connected) {
    m_connectAction->setEnabled(!connected);
    m_disconnectAction->setEnabled(connected);
    m_liveViewAction->setEnabled(connected);
    m_captureAction->setEnabled(connected);
    m_controls->setEnabledControls(connected);
}
