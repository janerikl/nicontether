#include "device/MtpController.h"
#include "device/MtpWorker.h"

MtpController::MtpController(QObject *parent) : QObject(parent) {
    qRegisterMetaType<MtpEntry>();
    qRegisterMetaType<QVector<MtpEntry>>();

    m_worker = new MtpWorker;
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &MtpWorker::deviceConnected, this, &MtpController::deviceConnected);
    connect(m_worker, &MtpWorker::deviceDisconnected, this, &MtpController::deviceDisconnected);
    connect(m_worker, &MtpWorker::filesListed, this, &MtpController::filesListed);
    connect(m_worker, &MtpWorker::thumbnailReady, this, &MtpController::thumbnailReady);
    connect(m_worker, &MtpWorker::importProgress, this, &MtpController::importProgress);
    connect(m_worker, &MtpWorker::importComplete, this, &MtpController::importComplete);
    connect(m_worker, &MtpWorker::deviceError, this, &MtpController::deviceError);
    connect(m_worker, &MtpWorker::log, this, &MtpController::log);

    m_thread.start();
    QMetaObject::invokeMethod(m_worker, "startPolling", Qt::QueuedConnection);
}

MtpController::~MtpController() {
    m_thread.quit();
    m_thread.wait();
}

void MtpController::refreshFiles() {
    QMetaObject::invokeMethod(m_worker, "refreshFiles", Qt::QueuedConnection);
}

void MtpController::importFiles(const QVector<MtpEntry> &entries, const QString &destDir) {
    MtpWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, entries, destDir]() { w->importFiles(entries, destDir); },
                              Qt::QueuedConnection);
}
