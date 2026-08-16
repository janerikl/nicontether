#include "device/MtpWorker.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

#include <libmtp.h>

namespace {

// Same browsable-image set BrowseTab uses for local folders (JPEG/RAW).
// MTP devices rarely expose fully raw formats over the phone's own DCIM, but
// filtering keeps things consistent and skips videos.
bool isBrowsableName(const QString &name) {
    static const QSet<QString> kExt = {
        "nef", "cr2", "cr3", "arw", "dng", "raf", "rw2", "orf",
        "jpg", "jpeg", "png", "tif", "tiff"};
    return kExt.contains(QFileInfo(name).suffix().toLower());
}

} // namespace

MtpWorker::MtpWorker(QObject *parent) : QObject(parent) {}

MtpWorker::~MtpWorker() {
    closeDevice();
}

void MtpWorker::startPolling() {
    if (!m_libmtpInited) {
        LIBMTP_Init();
        m_libmtpInited = true;
    }
    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(2500);
        connect(m_pollTimer, &QTimer::timeout, this, &MtpWorker::poll);
    }
    m_pollTimer->start();
    poll();
}

void MtpWorker::poll() {
    if (m_device) {
        // Already connected: confirm it is still reachable by re-listing the
        // root; libmtp has no hotplug-removal callback, so unplugs are only
        // detected the next time we try to talk to the device.
        LIBMTP_file_t *probe =
            LIBMTP_Get_Files_And_Folders(m_device, LIBMTP_FILES_AND_FOLDERS_ROOT,
                                         LIBMTP_FILES_AND_FOLDERS_ROOT);
        if (!probe) {
            closeDevice();
            emit deviceDisconnected();
        } else {
            LIBMTP_destroy_file_t(probe);
        }
        return;
    }
    if (openFirstDevice()) {
        char *friendly = LIBMTP_Get_Friendlyname(m_device);
        char *model = LIBMTP_Get_Modelname(m_device);
        QString name = friendly && *friendly ? QString::fromUtf8(friendly)
                       : model ? QString::fromUtf8(model)
                               : "Android Device";
        free(friendly);
        free(model);
        emit deviceConnected(name);
        emit log("MTP device connected: " + name);
        refreshFiles();
    }
}

bool MtpWorker::openFirstDevice() {
    LIBMTP_raw_device_t *rawDevices = nullptr;
    int count = 0;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawDevices, &count);
    if (err != LIBMTP_ERROR_NONE || count <= 0 || !rawDevices) {
        free(rawDevices);
        return false;
    }
    m_device = LIBMTP_Open_Raw_Device_Uncached(&rawDevices[0]);
    free(rawDevices);
    return m_device != nullptr;
}

void MtpWorker::closeDevice() {
    if (m_device) {
        LIBMTP_Release_Device(m_device);
        m_device = nullptr;
    }
}

void MtpWorker::collectImages(uint32_t storageId, uint32_t folderId,
                              QVector<MtpEntry> &out, int depth) {
    // DCIM trees are shallow (DCIM/Camera/...); cap recursion so a
    // pathological device can't stall the worker thread indefinitely.
    if (depth > 6) return;
    LIBMTP_file_t *list = LIBMTP_Get_Files_And_Folders(m_device, storageId, folderId);
    for (LIBMTP_file_t *f = list; f; f = f->next) {
        if (f->filetype == LIBMTP_FILETYPE_FOLDER) {
            collectImages(f->storage_id, f->item_id, out, depth + 1);
        } else if (f->filename && isBrowsableName(QString::fromUtf8(f->filename))) {
            MtpEntry entry;
            entry.id = f->item_id;
            entry.name = QString::fromUtf8(f->filename);
            entry.size = f->filesize;
            out << entry;
        }
    }
    LIBMTP_destroy_file_t(list);
}

void MtpWorker::refreshFiles() {
    if (!m_device) {
        emit deviceError("No device connected.");
        return;
    }

    // Prefer starting the walk at a top-level "DCIM" folder if present, to
    // avoid scanning the whole device (music, downloads, etc); fall back to
    // the root if the device doesn't expose one under that name.
    uint32_t startFolder = LIBMTP_FILES_AND_FOLDERS_ROOT;
    LIBMTP_file_t *rootList =
        LIBMTP_Get_Files_And_Folders(m_device, LIBMTP_FILES_AND_FOLDERS_ROOT,
                                     LIBMTP_FILES_AND_FOLDERS_ROOT);
    uint32_t dcimStorage = LIBMTP_FILES_AND_FOLDERS_ROOT;
    for (LIBMTP_file_t *f = rootList; f; f = f->next) {
        if (f->filetype == LIBMTP_FILETYPE_FOLDER && f->filename &&
            QString::fromUtf8(f->filename).compare("DCIM", Qt::CaseInsensitive) == 0) {
            startFolder = f->item_id;
            dcimStorage = f->storage_id;
            break;
        }
    }
    LIBMTP_destroy_file_t(rootList);

    QVector<MtpEntry> entries;
    collectImages(dcimStorage, startFolder, entries, 0);
    emit filesListed(entries);

    // Thumbnails are fetched after the list, one at a time; the device may
    // not support them for every item, in which case we simply skip it and
    // BrowseTab falls back to a placeholder icon.
    for (const MtpEntry &e : entries) {
        unsigned char *data = nullptr;
        unsigned int size = 0;
        if (LIBMTP_Get_Thumbnail(m_device, e.id, &data, &size) == 0 && data && size > 0) {
            QImage img;
            img.loadFromData(data, static_cast<int>(size));
            if (!img.isNull()) emit thumbnailReady(e.id, img);
        }
        free(data);
    }
}

void MtpWorker::importFiles(const QVector<MtpEntry> &entries, const QString &destDir) {
    if (!m_device) {
        emit deviceError("No device connected.");
        return;
    }
    QDir().mkpath(destDir);
    QStringList saved;
    int total = entries.size();
    for (int i = 0; i < total; ++i) {
        const MtpEntry &e = entries[i];
        emit importProgress(e.name, i, total);
        const QString dest = QDir(destDir).filePath(e.name);
        int ret = LIBMTP_Get_File_To_File(m_device, e.id, dest.toUtf8().constData(),
                                          nullptr, nullptr);
        if (ret == 0) {
            saved << dest;
        } else {
            emit deviceError("Failed to import: " + e.name);
        }
    }
    emit importProgress(QString(), total, total);
    emit importComplete(saved);
}
