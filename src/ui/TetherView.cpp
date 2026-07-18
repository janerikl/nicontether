#include "ui/TetherView.h"

#include "camera/CameraController.h"
#include "ui/LiveViewWidget.h"
#include "ui/ControlsPanel.h"
#include "ui/PreviewWindow.h"
#include "capture/NefPreview.h"

#include <QAction>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>

TetherView::TetherView(QWidget *parent) : QWidget(parent) {
    m_controller = new CameraController(this);
    buildUi();

    connect(m_controller, &CameraController::connected, this, &TetherView::handleConnected);
    connect(m_controller, &CameraController::disconnected, this, &TetherView::handleDisconnected);
    connect(m_controller, &CameraController::liveFrame,
            m_liveView, &LiveViewWidget::setFrame);
    connect(m_controller, &CameraController::captureComplete,
            this, &TetherView::handleCaptureComplete);
    connect(m_controller, &CameraController::captureStarted, this,
            [this] { emit statusMessage("Capturing…"); });
    connect(m_controller, &CameraController::configChanged, this,
            [this](const QString &w, const QString &v) {
                m_controls->updateValue(w, v);
                emit statusMessage(QString("Set %1 = %2").arg(w, v));
            });
    connect(m_controller, &CameraController::cameraError, this, &TetherView::handleError);
    connect(m_controller, &CameraController::log, this,
            [this](const QString &m) { emit statusMessage(m); });

    // Controls -> controller
    connect(m_controls, &ControlsPanel::configEditRequested,
            m_controller, &CameraController::setConfig);
    connect(m_controls, &ControlsPanel::autofocusRequested,
            m_controller, &CameraController::triggerAutofocus);
    connect(m_controls, &ControlsPanel::captureRequested,
            m_controller, &CameraController::capture);
    connect(m_liveView, &LiveViewWidget::focusRequested,
            m_controller, &CameraController::setAfArea);

    // Start with a default session so captures always have a home.
    m_controller->setSaveDirectory(m_session.startSession("studio"));
    setConnectedState(false);
}

void TetherView::buildUi() {
    // Actions are created here but placed on the HOST toolbar via tetherActions().
    m_connectAction = new QAction("Connect", this);
    m_disconnectAction = new QAction("Disconnect", this);
    m_liveViewAction = new QAction("Live View", this);
    m_liveViewAction->setCheckable(true);
    m_captureAction = new QAction("Capture (Space)", this);
    m_sessionAction = new QAction("New Session…", this);

    connect(m_connectAction, &QAction::triggered, this, &TetherView::onConnect);
    connect(m_disconnectAction, &QAction::triggered, this, &TetherView::onDisconnect);
    connect(m_liveViewAction, &QAction::toggled, this, &TetherView::onToggleLiveView);
    connect(m_captureAction, &QAction::triggered, m_controller, &CameraController::capture);
    connect(m_sessionAction, &QAction::triggered, this, &TetherView::onNewSession);

    // Spacebar triggers a capture while tethering — except on the Preview tab,
    // where Space is reserved for pan, and only while this view is active.
    m_captureShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_captureShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_captureShortcut, &QShortcut::activated,
            m_controller, &CameraController::capture);

    // Center: tabbed Live View / Preview fills the widget.
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_viewTabs = new QTabWidget;
    m_liveView = new LiveViewWidget;
    m_preview = new PreviewWindow;
    m_viewTabs->addTab(m_liveView, "Live View");
    m_viewTabs->addTab(m_preview, "Preview");
    vbox->addWidget(m_viewTabs, 1);

    connect(m_viewTabs, &QTabWidget::currentChanged, this, [this](int) {
        updateCaptureShortcut();
        if (m_viewTabs->currentWidget() == m_preview) m_preview->focusView();
    });

    m_controls = new ControlsPanel; // parented into the host's dock later
    updateCaptureShortcut();
}

QList<QAction *> TetherView::tetherActions() const {
    return { m_connectAction, m_disconnectAction, m_liveViewAction,
             m_captureAction, m_sessionAction };
}

void TetherView::setActive(bool active) {
    m_active = active;
    updateCaptureShortcut();
}

void TetherView::updateCaptureShortcut() {
    // Capture on Space only while tethering and not on the Preview tab.
    bool onPreview = m_viewTabs && m_viewTabs->currentWidget() == m_preview;
    if (m_captureShortcut) m_captureShortcut->setEnabled(m_active && !onPreview);
}

void TetherView::onConnect() {
    emit statusMessage("Connecting…");
    m_controller->connectCamera();
}

void TetherView::onDisconnect() {
    if (m_liveViewAction->isChecked()) m_liveViewAction->setChecked(false);
    m_controller->disconnectCamera();
}

void TetherView::onToggleLiveView(bool on) {
    if (on) m_controller->startLiveView();
    else { m_controller->stopLiveView(); m_liveView->clearFrame(); }
}

void TetherView::onNewSession() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Session",
                                         "Session name:", QLineEdit::Normal,
                                         "studio", &ok);
    if (!ok) return;
    QString dir = m_session.startSession(name);
    m_controller->setSaveDirectory(dir);
    emit statusMessage("Session: " + dir);
}

void TetherView::handleConnected(const QString &name, const ConfigOptionMap &options) {
    m_controls->populate(options);
    setConnectedState(true);
    emit statusMessage("Connected: " + name);
}

void TetherView::handleDisconnected() {
    setConnectedState(false);
    m_liveView->clearFrame();
    emit statusMessage("Disconnected");
}

void TetherView::handleCaptureComplete(const QString &path) {
    QImage preview = NefPreview::extract(path);
    // Notify the host so it can add the capture to the shared filmstrip, even if
    // no preview could be extracted (host draws a placeholder then).
    emit captureComplete(path);
    if (!preview.isNull()) {
        m_preview->showImage(path, preview);
        m_viewTabs->setCurrentWidget(m_preview);
        emit statusMessage("Captured: " + path);
    } else {
        emit statusMessage("Captured (no embedded preview): " + path);
    }
}

void TetherView::handleError(const QString &message) {
    emit statusMessage("Error: " + message);
    QMessageBox::warning(this, "Camera Error", message);
}

void TetherView::setConnectedState(bool connected) {
    m_connectAction->setEnabled(!connected);
    m_disconnectAction->setEnabled(connected);
    m_liveViewAction->setEnabled(connected);
    m_captureAction->setEnabled(connected);
    m_controls->setEnabledControls(connected);
}
