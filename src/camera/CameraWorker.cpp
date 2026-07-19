#include "camera/CameraWorker.h"

#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2-context.h>

namespace {

// Logical control -> candidate gphoto2 widget names (first that exists wins).
// Nikon models name these inconsistently, so we probe several.
const struct {
    const char *key;
    const char *label;
    QStringList candidates;
} kLogicalControls[] = {
    {"shutterspeed", "Shutter", {"shutterspeed", "shutterspeed2"}},
    {"aperture",     "Aperture", {"f-number", "aperture"}},
    {"iso",          "ISO", {"iso", "isospeed", "iso-speed"}},
    {"whitebalance", "White Balance", {"whitebalance", "whitebalance2"}},
    {"imagequality", "Quality", {"imagequality", "imagequality2", "imgquality"}},
};

} // namespace

CameraWorker::CameraWorker(QObject *parent) : QObject(parent) {}

CameraWorker::~CameraWorker() {
    disconnectCamera();
}

void CameraWorker::connectCamera() {
    if (m_cam) {
        emit log("Camera already connected.");
        return;
    }
    m_ctx = gp_context_new();
    if (gp_camera_new(&m_cam) != GP_OK) {
        emit cameraError("Failed to allocate camera handle.");
        return;
    }
    int ret = gp_camera_init(m_cam, m_ctx);
    // The desktop's gvfs auto-mounts the camera on plug-in, which holds the USB
    // claim and makes gp_camera_init fail with GP_ERROR_IO_USB_CLAIM. Unmount it
    // and retry once so the user never has to run `gio mount -u` by hand.
    if (ret == GP_ERROR_IO_USB_CLAIM && releaseGvfsCameraMounts()) {
        ret = gp_camera_init(m_cam, m_ctx);
    }
    if (ret != GP_OK) {
        QString hint;
        if (ret == GP_ERROR_IO_USB_CLAIM)
            hint = " The device is auto-mounted by the desktop and could not be"
                   " released automatically. Try unplugging and replugging it,"
                   " or run: gio mount -s gphoto2";
        gp_camera_free(m_cam);
        m_cam = nullptr;
        emit cameraError(QString("Could not connect to camera (%1).%2")
                             .arg(gp_result_as_string(ret)).arg(hint));
        return;
    }

    // Read model name from abilities.
    CameraAbilities abilities;
    QString name = "Nikon Camera";
    if (gp_camera_get_abilities(m_cam, &abilities) == GP_OK)
        name = QString::fromUtf8(abilities.model);

    ConfigOptionMap options = readConfigTree();
    emit connected(name, options);
    emit log("Connected: " + name);
}

bool CameraWorker::releaseGvfsCameraMounts() {
    const QString gio = QStandardPaths::findExecutable("gio");
    if (gio.isEmpty()) {
        emit log("Camera is auto-mounted but 'gio' was not found to release it.");
        return false;
    }
    // Unmount every gvfs mount using the gphoto2 scheme, ignoring any pending
    // file operations. This drops the volume monitor's USB claim.
    emit log("Camera is auto-mounted; releasing it via gio...");
    QProcess proc;
    proc.start(gio, {"mount", "-s", "gphoto2", "-f"});
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(1000);
        emit log("Timed out while releasing the auto-mounted camera.");
        return false;
    }
    // gvfs needs a moment to fully drop the USB claim after unmounting.
    QThread::msleep(300);
    return true;
}

void CameraWorker::disconnectCamera() {
    stopLiveView();
    if (m_cam) {
        gp_camera_exit(m_cam, m_ctx);
        gp_camera_free(m_cam);
        m_cam = nullptr;
    }
    if (m_ctx) {
        gp_context_unref(m_ctx);
        m_ctx = nullptr;
    }
    emit disconnected();
}

// Locate a widget by name in a freshly-read config tree. Caller owns *rootOut
// and must gp_widget_free() it. Returns false if not found.
bool CameraWorker::findWidget(const char *name, void **widgetOut, void **rootOut) {
    *widgetOut = nullptr;
    *rootOut = nullptr;
    if (!m_cam) return false;
    CameraWidget *root = nullptr;
    if (gp_camera_get_config(m_cam, &root, m_ctx) != GP_OK) return false;
    CameraWidget *child = nullptr;
    if (gp_widget_get_child_by_name(root, name, &child) != GP_OK) {
        gp_widget_free(root);
        return false;
    }
    *rootOut = root;
    *widgetOut = child;
    return true;
}

ConfigOptionMap CameraWorker::readConfigTree() {
    ConfigOptionMap map;
    if (!m_cam) return map;

    CameraWidget *root = nullptr;
    if (gp_camera_get_config(m_cam, &root, m_ctx) != GP_OK) return map;

    for (const auto &ctrl : kLogicalControls) {
        CameraWidget *child = nullptr;
        QString foundName;
        for (const QString &cand : ctrl.candidates) {
            if (gp_widget_get_child_by_name(root, cand.toUtf8().constData(), &child) == GP_OK) {
                foundName = cand;
                break;
            }
        }
        if (!child) continue;

        ConfigOption opt;
        opt.key = ctrl.key;
        opt.widgetName = foundName;
        opt.label = ctrl.label;

        const char *val = nullptr;
        if (gp_widget_get_value(child, &val) == GP_OK && val)
            opt.current = QString::fromUtf8(val);

        int readonly = 0;
        gp_widget_get_readonly(child, &readonly);
        opt.readOnly = readonly != 0;

        int n = gp_widget_count_choices(child);
        for (int i = 0; i < n; ++i) {
            const char *choice = nullptr;
            if (gp_widget_get_choice(child, i, &choice) == GP_OK && choice)
                opt.choices << QString::fromUtf8(choice);
        }
        map.insert(ctrl.key, opt);
    }

    gp_widget_free(root);
    return map;
}

void CameraWorker::setConfig(const QString &widgetName, const QString &value) {
    void *w = nullptr, *r = nullptr;
    if (!findWidget(widgetName.toUtf8().constData(), &w, &r)) {
        emit cameraError("Control not found: " + widgetName);
        return;
    }
    CameraWidget *child = static_cast<CameraWidget *>(w);
    CameraWidget *root = static_cast<CameraWidget *>(r);

    int ret = gp_widget_set_value(child, value.toUtf8().constData());
    if (ret == GP_OK)
        ret = gp_camera_set_config(m_cam, root, m_ctx);
    gp_widget_free(root);

    if (ret != GP_OK) {
        reportError("set " + widgetName, ret);
        return;
    }
    emit configChanged(widgetName, value);
}

void CameraWorker::triggerAutofocus() {
    void *w = nullptr, *r = nullptr;
    if (!findWidget("autofocusdrive", &w, &r)) {
        emit log("Autofocus control not available on this camera.");
        return;
    }
    CameraWidget *child = static_cast<CameraWidget *>(w);
    CameraWidget *root = static_cast<CameraWidget *>(r);
    int on = 1;
    gp_widget_set_value(child, &on);
    int ret = gp_camera_set_config(m_cam, root, m_ctx);
    gp_widget_free(root);
    if (ret != GP_OK) reportError("autofocus", ret);
}

void CameraWorker::setAfArea(int x, int y) {
    void *w = nullptr, *r = nullptr;
    if (!findWidget("changeafarea", &w, &r)) {
        emit log("Focus-point selection not available on this camera.");
        emit afAreaResult(false);
        return;
    }
    CameraWidget *child = static_cast<CameraWidget *>(w);
    CameraWidget *root = static_cast<CameraWidget *>(r);
    QString coord = QString("%1x%2").arg(x).arg(y);
    gp_widget_set_value(child, coord.toUtf8().constData());
    int ret = gp_camera_set_config(m_cam, root, m_ctx);
    gp_widget_free(root);
    if (ret != GP_OK) {
        reportError("set AF area", ret);
        emit afAreaResult(false);
        return;
    }
    triggerAutofocus();
    emit afAreaResult(true);
}

void CameraWorker::startLiveView() {
    if (!m_cam) return;
    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(40); // ~25 fps target
        connect(m_liveTimer, &QTimer::timeout, this, &CameraWorker::grabPreviewFrame);
    }
    m_liveViewActive = true;
    m_liveTimer->start();
}

void CameraWorker::stopLiveView() {
    m_liveViewActive = false;
    if (m_liveTimer) m_liveTimer->stop();
}

void CameraWorker::grabPreviewFrame() {
    if (!m_cam) return;
    CameraFile *file = nullptr;
    if (gp_file_new(&file) != GP_OK) return;

    int ret = gp_camera_capture_preview(m_cam, file, m_ctx);
    if (ret == GP_OK) {
        const char *data = nullptr;
        unsigned long size = 0;
        if (gp_file_get_data_and_size(file, &data, &size) == GP_OK && size > 0) {
            QImage img;
            if (img.loadFromData(reinterpret_cast<const uchar *>(data),
                                 static_cast<int>(size)))
                emit liveFrame(img);
        }
    }
    gp_file_free(file);
}

void CameraWorker::capture() {
    if (!m_cam) {
        emit cameraError("No camera connected.");
        return;
    }
    bool wasLive = m_liveViewActive;
    if (wasLive) { m_liveViewActive = false; if (m_liveTimer) m_liveTimer->stop(); }

    emit captureStarted();

    CameraFilePath camPath;
    int ret = gp_camera_capture(m_cam, GP_CAPTURE_IMAGE, &camPath, m_ctx);
    if (ret != GP_OK) {
        reportError("capture", ret);
        if (wasLive) startLiveView();
        return;
    }

    CameraFile *file = nullptr;
    gp_file_new(&file);
    ret = gp_camera_file_get(m_cam, camPath.folder, camPath.name,
                             GP_FILE_TYPE_NORMAL, file, m_ctx);
    if (ret != GP_OK) {
        reportError("download", ret);
        gp_file_free(file);
        if (wasLive) startLiveView();
        return;
    }

    QDir().mkpath(m_saveDir);
    QString dest = QDir(m_saveDir).filePath(QString::fromUtf8(camPath.name));
    ret = gp_file_save(file, dest.toUtf8().constData());
    gp_file_free(file);

    // Remove from camera buffer so it doesn't accumulate.
    gp_camera_file_delete(m_cam, camPath.folder, camPath.name, m_ctx);

    if (ret != GP_OK) {
        reportError("save", ret);
    } else {
        emit captureComplete(dest);
        emit log("Captured: " + dest);
    }

    if (wasLive) startLiveView();
}

void CameraWorker::reportError(const QString &context, int gpCode) {
    emit cameraError(QString("%1 failed: %2")
                         .arg(context).arg(gp_result_as_string(gpCode)));
}
