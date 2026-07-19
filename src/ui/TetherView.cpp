#include "ui/TetherView.h"

#include "camera/CameraController.h"
#include "ui/LiveViewWidget.h"
#include "ui/ControlsPanel.h"

#include <QAction>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

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
    connect(m_controller, &CameraController::configRefreshed,
            m_controls, &ControlsPanel::populate);

    // Controls -> controller
    connect(m_controls, &ControlsPanel::configEditRequested,
            m_controller, &CameraController::setConfig);
    connect(m_controls, &ControlsPanel::autofocusRequested,
            m_controller, &CameraController::triggerAutofocus);
    connect(m_controls, &ControlsPanel::captureRequested,
            m_controller, &CameraController::capture);

    connect(m_liveView, &LiveViewWidget::focusRequested,
            m_controller, &CameraController::setAfArea);
    connect(m_controller, &CameraController::afAreaResult,
            m_liveView, &LiveViewWidget::setAfResult);
    connect(m_liveView, &LiveViewWidget::calibrationPointPicked, this,
            [this](double nx, double ny) {
                if (!m_calibrating) return;
                if (m_calibrator.stage() == AfCalibrator::Stage::AwaitTarget) {
                    m_calibrator.setTarget(nx, ny);
                    fireCalibrationAf();
                    return;
                }
                if (m_calibrator.stage() == AfCalibrator::Stage::AwaitObserved) {
                    if (m_calibrator.setObserved(nx, ny)) {
                        int w = m_calibrator.resultW(), h = m_calibrator.resultH();
                        endCalibration(true);
                        emit calibrationFinished(w, h);
                    } else {
                        QMessageBox::warning(this, "Calibration",
                            "Couldn't derive a sensible AF frame size from those "
                            "clicks.\n\n"
                            "If the sharp area was only a little off from your "
                            "target, pick a target closer to a corner and try "
                            "again.\n\n"
                            "If the sharp area was in a completely different "
                            "part of the frame, this isn't a calibration problem "
                            "-- it means the camera itself isn't focusing where "
                            "requested. Check the camera body's AF-area mode: "
                            "set it to Single-point AF (not Auto-area/Wide-area/"
                            "3D-tracking) and AF-S (not AF-C), then retry.");
                        m_calibrator.begin(m_afFrameW, m_afFrameH);
                        m_liveView->setCalibrationCrosshair(false);
                        updateCalibrationPrompt();
                    }
                }
            });

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

    // Spacebar triggers a capture while tethering, only while this view is active.
    m_captureShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_captureShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_captureShortcut, &QShortcut::activated,
            m_controller, &CameraController::capture);

    // Center: the live view fills the widget.
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    m_liveView = new LiveViewWidget;
    vbox->addWidget(m_liveView, 1);

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
    // Capture on Space only while tethering.
    if (m_captureShortcut) m_captureShortcut->setEnabled(m_active);
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
    emit cameraConnected(name);
}

void TetherView::setAfFrameSize(int w, int h) {
    m_afFrameW = w;
    m_afFrameH = h;
    m_liveView->setAfFrameSize(w, h);
}

void TetherView::buildCalibrationPanel() {
    if (m_calibPanel) return;
    m_calibPanel = new QFrame(m_liveView);
    m_calibPanel->setFrameShape(QFrame::StyledPanel);
    m_calibPanel->setAutoFillBackground(true);
    m_calibPanel->setStyleSheet(
        "QFrame { background: rgba(20,20,20,220); border-radius: 8px; }"
        "QLabel { color: white; }");

    auto *v = new QVBoxLayout(m_calibPanel);
    m_calibLabel = new QLabel;
    m_calibLabel->setWordWrap(true);
    m_calibLabel->setMinimumWidth(360);
    v->addWidget(m_calibLabel);

    auto *row2 = new QHBoxLayout;
    m_calibRefire = new QPushButton("Re-fire");
    m_calibCancel = new QPushButton("Cancel");
    row2->addStretch(1);
    row2->addWidget(m_calibRefire);
    row2->addWidget(m_calibCancel);
    v->addLayout(row2);

    connect(m_calibRefire, &QPushButton::clicked, this, [this] {
        if (m_calibrating && m_calibrator.stage() == AfCalibrator::Stage::AwaitObserved)
            fireCalibrationAf();
    });
    connect(m_calibCancel, &QPushButton::clicked, this,
            [this] { endCalibration(false); });
}

void TetherView::positionCalibrationPanel() {
    if (!m_calibPanel) return;
    m_calibPanel->adjustSize();
    int x = (m_liveView->width() - m_calibPanel->width()) / 2;
    m_calibPanel->move(qMax(0, x), 12);
    m_calibPanel->raise();
}

void TetherView::startCalibration() {
    if (!m_liveViewAction->isChecked()) {
        emit statusMessage("Start Live View before calibrating.");
        return;
    }
    buildCalibrationPanel();
    m_calibrating = true;
    m_calibrator.begin(m_afFrameW, m_afFrameH);
    m_liveView->setCalibrationMode(true);
    m_liveView->setCalibrationCrosshair(false);
    updateCalibrationPrompt();
    m_calibPanel->show();
    positionCalibrationPanel();
}

void TetherView::fireCalibrationAf() {
    int afx = 0, afy = 0;
    m_calibrator.afCommand(afx, afy);
    m_controller->setAfArea(afx, afy);
    updateCalibrationPrompt();
}

void TetherView::updateCalibrationPrompt() {
    if (!m_calibLabel) return;
    if (m_calibrator.stage() == AfCalibrator::Stage::AwaitTarget) {
        m_calibLabel->setText(
            "<b>AF calibration</b><br>"
            "Aim at a scene with depth across the frame (e.g. a tape measure "
            "receding from the camera). For the best result, click a target "
            "off-center in both directions (e.g. near a corner) &mdash; "
            "that's the point you want in focus.");
    } else {
        m_calibLabel->setText(
            "<b>AF calibration</b><br>"
            "AF fired at your target. Watch which part of the live view "
            "actually snapped sharp, then click that spot. Use Re-fire if "
            "you need to repeat the AF attempt first.");
    }
    positionCalibrationPanel();
}

void TetherView::endCalibration(bool /*finished*/) {
    m_calibrating = false;
    m_liveView->setCalibrationMode(false);
    m_liveView->setCalibrationCrosshair(false);
    if (m_calibPanel) m_calibPanel->hide();
}

void TetherView::handleDisconnected() {
    setConnectedState(false);
    m_liveView->clearFrame();
    emit statusMessage("Disconnected");
}

void TetherView::handleCaptureComplete(const QString &path) {
    // Notify the host so it can add the capture to the shared filmstrip; the
    // captured image is reviewed by clicking its thumbnail to open Retouch.
    emit captureComplete(path);
    emit statusMessage("Captured: " + path);
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
