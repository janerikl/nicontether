#pragma once

#include <QObject>
#include <QThread>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include "device/MtpEntry.h"

class MtpWorker;

// GUI-thread facade for MtpWorker. Owns the worker thread and forwards
// commands as queued calls; re-emits worker signals so the UI only ever
// talks to this object. Mirrors CameraController's shape.
class MtpController : public QObject {
    Q_OBJECT
public:
    explicit MtpController(QObject *parent = nullptr);
    ~MtpController() override;

    void refreshFiles();
    void importFiles(const QVector<MtpEntry> &entries, const QString &destDir);

signals:
    void deviceConnected(const QString &name);
    void deviceDisconnected();
    void filesListed(const QVector<MtpEntry> &entries);
    void thumbnailReady(quint32 id, const QImage &image);
    void importProgress(const QString &fileName, int done, int total);
    void importComplete(const QStringList &savedPaths);
    void deviceError(const QString &message);
    void log(const QString &message);

private:
    QThread m_thread;
    MtpWorker *m_worker = nullptr;
};
