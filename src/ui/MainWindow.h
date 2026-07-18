#pragma once

#include <QMainWindow>
#include <QStringList>

#include "capture/SessionManager.h"
#include "camera/CameraSettings.h"

class CameraController;
class LiveViewWidget;
class ControlsPanel;
class FilmstripWidget;
class PreviewWindow;
class RetouchWindow;
class QLabel;
class QAction;
class QDockWidget;
class QTabWidget;
class QShortcut;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onConnect();
    void onDisconnect();
    void onToggleLiveView(bool on);
    void onNewSession();

    void handleConnected(const QString &name, const ConfigOptionMap &options);
    void handleDisconnected();
    void handleCaptureComplete(const QString &path);
    void handleError(const QString &message);
    void showPreviewFor(const QString &path);
    void openInRetouch(const QString &path); // empty path = most recent capture

private:
    void buildUi();
    void setConnectedState(bool connected);

    CameraController *m_controller = nullptr;
    SessionManager m_session;
    RetouchWindow *m_retouchWindow = nullptr;
    QStringList m_captures; // paths of all captures this session

    QTabWidget *m_viewTabs = nullptr;
    LiveViewWidget *m_liveView = nullptr;
    ControlsPanel *m_controls = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    PreviewWindow *m_preview = nullptr;
    QDockWidget *m_controlsDock = nullptr;

    QLabel *m_statusLabel = nullptr;
    QAction *m_connectAction = nullptr;
    QAction *m_disconnectAction = nullptr;
    QAction *m_liveViewAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_sessionAction = nullptr;
    QShortcut *m_captureShortcut = nullptr;
};
