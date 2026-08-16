#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>

#include "device/MtpEntry.h"

struct LIBMTP_mtpdevice_struct;
typedef struct LIBMTP_mtpdevice_struct LIBMTP_mtpdevice_t;

class QTimer;

// Owns the libmtp device handle. All libmtp calls happen here, on a dedicated
// thread: libmtp is blocking and device polling would otherwise stall the
// GUI, matching the CameraWorker/CameraController split used for tethering.
class MtpWorker : public QObject {
    Q_OBJECT
public:
    explicit MtpWorker(QObject *parent = nullptr);
    ~MtpWorker() override;

public slots:
    // Starts periodic polling for a connected Android device. Call once,
    // after moveToThread, so the poll timer lives on this object's thread.
    void startPolling();
    // Re-scans the currently connected device's DCIM tree for images.
    void refreshFiles();
    // Downloads the given entries into destDir, one at a time.
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

private slots:
    void poll();

private:
    bool openFirstDevice();
    void closeDevice();
    // Recursively walks folderId collecting browsable image files into out.
    void collectImages(uint32_t storageId, uint32_t folderId, QVector<MtpEntry> &out, int depth);

    LIBMTP_mtpdevice_t *m_device = nullptr;
    QTimer *m_pollTimer = nullptr;
    bool m_libmtpInited = false;
};
