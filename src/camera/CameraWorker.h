#pragma once

#include <QObject>
#include <QImage>
#include <QString>

#include "camera/CameraSettings.h"

struct _Camera;
struct _GPContext;
typedef struct _Camera Camera;
typedef struct _GPContext GPContext;

class QTimer;

// Owns the libgphoto2 Camera + context. All gphoto2 calls happen here, on a
// dedicated thread. libgphoto2 is not thread-safe per camera and its calls
// block, so the GUI must only ever reach this object via queued signals/slots.
class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker() override;

public slots:
    void connectCamera();
    void disconnectCamera();
    void startLiveView();
    void stopLiveView();
    void capture();
    void setConfig(const QString &widgetName, const QString &value);
    void triggerAutofocus();
    void setAfArea(int x, int y); // sensor pixel coordinates

signals:
    void connected(const QString &cameraName, const ConfigOptionMap &options);
    void disconnected();
    void liveFrame(const QImage &frame);
    void captureStarted();
    void captureComplete(const QString &savedPath);
    void configChanged(const QString &widgetName, const QString &value);
    void cameraError(const QString &message);
    void log(const QString &message);

private slots:
    void grabPreviewFrame();

private:
    ConfigOptionMap readConfigTree();
    bool findWidget(const char *name, void **widgetOut, void **rootOut);
    void reportError(const QString &context, int gpCode);
    // Unmounts any gvfs-mounted cameras that would otherwise hold the USB
    // claim. Returns true if an unmount was performed. Best-effort.
    bool releaseGvfsCameraMounts();

    GPContext *m_ctx = nullptr;
    Camera *m_cam = nullptr;
    QTimer *m_liveTimer = nullptr;
    bool m_liveViewActive = false;
    QString m_saveDir; // set by SessionManager via setConfig-like path; see below

public:
    // Where captured NEF files are written. Set before capture().
    void setSaveDirectory(const QString &dir) { m_saveDir = dir; }
};
