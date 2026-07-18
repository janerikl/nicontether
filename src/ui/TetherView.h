#pragma once

#include <QWidget>
#include <QList>

#include "capture/SessionManager.h"
#include "camera/CameraSettings.h"

class CameraController;
class LiveViewWidget;
class ControlsPanel;
class PreviewWindow;
class QAction;
class QTabWidget;
class QShortcut;

// Self-contained tethering view: owns the camera pipeline (controller, session),
// the live-view / preview tabs, the camera ControlsPanel, and the tether actions
// (Connect / Disconnect / Live View / Capture / New Session). Embed it as a page
// in a host window; the host places controlsPanel() in a dock and tetherActions()
// on a toolbar, and listens for captureComplete()/statusMessage().
class TetherView : public QWidget {
    Q_OBJECT
public:
    explicit TetherView(QWidget *parent = nullptr);

    // Camera ControlsPanel widget, for the host to place in a dock.
    ControlsPanel *controlsPanel() const { return m_controls; }
    // Tether actions in display order, for the host toolbar.
    QList<QAction *> tetherActions() const;

    // Enable/disable the app-wide spacebar capture shortcut. The host calls
    // setActive(true) when the tether page is shown, false otherwise, so Space
    // only captures while tethering.
    void setActive(bool active);

signals:
    void captureComplete(const QString &path);
    void statusMessage(const QString &message);

private slots:
    void onConnect();
    void onDisconnect();
    void onToggleLiveView(bool on);
    void onNewSession();
    void handleConnected(const QString &name, const ConfigOptionMap &options);
    void handleDisconnected();
    void handleCaptureComplete(const QString &path);
    void handleError(const QString &message);

private:
    void buildUi();
    void setConnectedState(bool connected);
    void updateCaptureShortcut();

    CameraController *m_controller = nullptr;
    SessionManager m_session;

    QTabWidget *m_viewTabs = nullptr;
    LiveViewWidget *m_liveView = nullptr;
    ControlsPanel *m_controls = nullptr;
    PreviewWindow *m_preview = nullptr;

    QAction *m_connectAction = nullptr;
    QAction *m_disconnectAction = nullptr;
    QAction *m_liveViewAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_sessionAction = nullptr;
    QShortcut *m_captureShortcut = nullptr;
    bool m_active = false;
};
