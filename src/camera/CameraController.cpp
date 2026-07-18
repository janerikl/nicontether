#include "camera/CameraController.h"
#include "camera/CameraWorker.h"

CameraController::CameraController(QObject *parent) : QObject(parent) {
    qRegisterMetaType<ConfigOption>();
    qRegisterMetaType<ConfigOptionMap>();

    m_worker = new CameraWorker;
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Re-emit worker signals to the GUI (queued across the thread boundary).
    connect(m_worker, &CameraWorker::connected, this, &CameraController::connected);
    connect(m_worker, &CameraWorker::disconnected, this, &CameraController::disconnected);
    connect(m_worker, &CameraWorker::liveFrame, this, &CameraController::liveFrame);
    connect(m_worker, &CameraWorker::captureStarted, this, &CameraController::captureStarted);
    connect(m_worker, &CameraWorker::captureComplete, this, &CameraController::captureComplete);
    connect(m_worker, &CameraWorker::configChanged, this, &CameraController::configChanged);
    connect(m_worker, &CameraWorker::cameraError, this, &CameraController::cameraError);
    connect(m_worker, &CameraWorker::log, this, &CameraController::log);

    m_thread.start();
}

CameraController::~CameraController() {
    // Ensure a clean gphoto2 shutdown on the worker thread.
    QMetaObject::invokeMethod(m_worker, "disconnectCamera", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void CameraController::connectCamera() {
    QMetaObject::invokeMethod(m_worker, "connectCamera", Qt::QueuedConnection);
}
void CameraController::disconnectCamera() {
    QMetaObject::invokeMethod(m_worker, "disconnectCamera", Qt::QueuedConnection);
}
void CameraController::startLiveView() {
    QMetaObject::invokeMethod(m_worker, "startLiveView", Qt::QueuedConnection);
}
void CameraController::stopLiveView() {
    QMetaObject::invokeMethod(m_worker, "stopLiveView", Qt::QueuedConnection);
}
void CameraController::capture() {
    QMetaObject::invokeMethod(m_worker, "capture", Qt::QueuedConnection);
}
void CameraController::setConfig(const QString &widgetName, const QString &value) {
    QMetaObject::invokeMethod(m_worker, "setConfig", Qt::QueuedConnection,
                              Q_ARG(QString, widgetName), Q_ARG(QString, value));
}
void CameraController::triggerAutofocus() {
    QMetaObject::invokeMethod(m_worker, "triggerAutofocus", Qt::QueuedConnection);
}
void CameraController::setAfArea(int x, int y) {
    QMetaObject::invokeMethod(m_worker, "setAfArea", Qt::QueuedConnection,
                              Q_ARG(int, x), Q_ARG(int, y));
}
void CameraController::setSaveDirectory(const QString &dir) {
    CameraWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, dir]() { w->setSaveDirectory(dir); },
                              Qt::QueuedConnection);
}
