#pragma once

#include <QObject>
#include <QThread>
#include <QImage>

#include "camera/CameraSettings.h"

class CameraWorker;

// GUI-thread facade. Owns the worker thread and forwards commands as queued
// calls; re-emits worker signals so the UI only ever talks to this object.
class CameraController : public QObject {
    Q_OBJECT
public:
    explicit CameraController(QObject *parent = nullptr);
    ~CameraController() override;

    // Commands (safe to call from the GUI thread).
    void connectCamera();
    void disconnectCamera();
    void startLiveView();
    void stopLiveView();
    void capture();
    void setConfig(const QString &widgetName, const QString &value);
    void triggerAutofocus();
    void setAfArea(int x, int y);
    void setSaveDirectory(const QString &dir);

signals:
    void connected(const QString &cameraName, const ConfigOptionMap &options);
    void disconnected();
    void liveFrame(const QImage &frame);
    void captureStarted();
    void captureComplete(const QString &savedPath);
    void configChanged(const QString &widgetName, const QString &value);
    void cameraError(const QString &message);
    void log(const QString &message);

private:
    QThread m_thread;
    CameraWorker *m_worker = nullptr;
};
