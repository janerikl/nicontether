#include "edit/RetouchTab.h"
#include "edit/ImageCanvas.h"
#include "edit/RawLoader.h"
#include "edit/EditSidecar.h"

#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>

void RenderWorker::render(const QImage &src, const Adjustments &adj) {
    emit done(applyAdjustments(src, adj));
}

namespace {
constexpr int kDisplayMaxDim = 1600; // interactive preview resolution cap

Adjustments geometryOnly(const Adjustments &a, bool includeCrop) {
    Adjustments g;
    g.rotationQuadrants = a.rotationQuadrants;
    g.flipH = a.flipH;
    g.flipV = a.flipV;
    g.cropRect = includeCrop ? a.cropRect : QRect();
    return g; // tone fields left at neutral 0
}

Adjustments toneOnly(const Adjustments &a) {
    Adjustments t = a; // copy all tone/colour/detail fields
    t.rotationQuadrants = 0;
    t.flipH = t.flipV = false;
    t.cropRect = QRect();
    return t; // geometry neutralised
}

bool geometryDiffers(const Adjustments &a, const Adjustments &b) {
    return a.rotationQuadrants != b.rotationQuadrants || a.flipH != b.flipH ||
           a.flipV != b.flipV || a.cropRect != b.cropRect;
}
} // namespace

RetouchTab::RetouchTab(const QString &path, QWidget *parent)
    : QWidget(parent), m_path(path) {
    // Restore previously-saved edits, if any (does not mark dirty).
    EditSidecar::load(m_path, m_adj);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new ImageCanvas;
    layout->addWidget(m_canvas);
    connect(m_canvas, &ImageCanvas::cropSelected, this, &RetouchTab::onCanvasCrop);
    connect(m_canvas, &ImageCanvas::commitCropRequested, this, &RetouchTab::applyCrop);
    connect(m_canvas, &ImageCanvas::colorPicked, this, &RetouchTab::onColorPicked);
    connect(m_canvas, &ImageCanvas::zoomChanged, this, &RetouchTab::zoomChanged);

    m_canvas->setPlaceholder("Decoding RAW…");

    // After dragging stops, upgrade the preview with the expensive convolutions.
    m_fullRenderTimer = new QTimer(this);
    m_fullRenderTimer->setSingleShot(true);
    m_fullRenderTimer->setInterval(140);
    connect(m_fullRenderTimer, &QTimer::timeout, this, &RetouchTab::retoneFull);

    // Render worker on its own thread so applyAdjustments never blocks the GUI.
    qRegisterMetaType<Adjustments>("Adjustments");
    m_renderThread = new QThread(this);
    m_renderWorker = new RenderWorker;
    m_renderWorker->moveToThread(m_renderThread);
    connect(m_renderThread, &QThread::finished, m_renderWorker, &QObject::deleteLater);
    connect(m_renderWorker, &RenderWorker::done, this, &RetouchTab::onRenderDone);
    m_renderThread->start();

    // Decode off the GUI thread.
    m_watcher = new QFutureWatcher<QImage>(this);
    connect(m_watcher, &QFutureWatcher<QImage>::finished, this,
            &RetouchTab::onDecodeFinished);
    m_watcher->setFuture(QtConcurrent::run(RawLoader::load, m_path));
}

RetouchTab::~RetouchTab() {
    if (m_watcher) {
        m_watcher->waitForFinished(); // ensure the decode worker isn't writing after free
    }
    if (m_renderThread) {
        m_renderThread->quit();
        m_renderThread->wait();
    }
}

void RetouchTab::onDecodeFinished() {
    m_base = m_watcher->result();
    if (m_base.isNull()) {
        m_canvas->setPlaceholder("Failed to decode RAW");
        emit decoded(false);
        return;
    }
    rebuildGeom();
    emit decoded(true);
    // Reflect any restored edits in the badge (clean, but possibly has edits).
    emit editStateChanged(m_dirty, hasEdits());
}

bool RetouchTab::hasEdits() const {
    return hasToneEdits(m_adj) || m_adj.rotationQuadrants != 0 || m_adj.flipH ||
           m_adj.flipV || !m_adj.cropRect.isNull();
}

void RetouchTab::markEdited() {
    m_dirty = true;
    emit editStateChanged(true, hasEdits());
}

void RetouchTab::saveEdits() {
    EditSidecar::save(m_path, m_adj);
    m_dirty = false;
    emit editStateChanged(false, hasEdits());
}

void RetouchTab::rebuildGeom() {
    if (m_base.isNull()) return;
    m_geomImg = applyAdjustments(m_base, geometryOnly(m_adj, !m_cropMode));
    if (qMax(m_geomImg.width(), m_geomImg.height()) > kDisplayMaxDim) {
        m_scaled = m_geomImg.scaled(kDisplayMaxDim, kDisplayMaxDim,
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        m_scaled = m_geomImg;
    }
    m_scaleFromGeom = m_geomImg.width() > 0
                          ? double(m_scaled.width()) / m_geomImg.width()
                          : 1.0;
    retone();
}

void RetouchTab::retone() {
    if (m_scaled.isNull()) return;
    // Fast interactive path: skip the clarity/sharpen blur convolutions (the
    // expensive part) while the user is dragging, then schedule a full render.
    Adjustments t = toneOnly(m_adj);
    bool heavy = (t.clarity != 0 || t.sharpen != 0);
    if (heavy) { t.clarity = 0; t.sharpen = 0; }
    requestRender(m_scaled, t);
    if (heavy) m_fullRenderTimer->start();
    else m_fullRenderTimer->stop();
}

void RetouchTab::retoneFull() {
    if (m_scaled.isNull()) return;
    requestRender(m_scaled, toneOnly(m_adj));
}

// Coalesced async render: at most one job in flight; newer requests overwrite
// the pending one so intermediate drag frames are dropped, not queued.
void RetouchTab::requestRender(const QImage &src, const Adjustments &adj) {
    if (m_rendering) {
        m_pendingSrc = src;
        m_pendingAdj = adj;
        m_hasPending = true;
        return;
    }
    m_rendering = true;
    QMetaObject::invokeMethod(m_renderWorker, "render", Qt::QueuedConnection,
                              Q_ARG(QImage, src), Q_ARG(Adjustments, adj));
}

void RetouchTab::onRenderDone(const QImage &result) {
    m_lastEdited = result;
    if (!m_showingOriginal) m_canvas->setImage(result);
    m_rendering = false;
    if (m_hasPending) {
        m_hasPending = false;
        requestRender(m_pendingSrc, m_pendingAdj);
    }
}

// Press-and-hold before/after. "Original" is the framed base without tone/
// colour/detail edits (m_scaled is oriented+cropped but untoned).
void RetouchTab::showOriginal(bool on) {
    if (m_scaled.isNull()) return;
    m_showingOriginal = on;
    if (on) {
        m_canvas->setImage(m_scaled);
    } else if (!m_lastEdited.isNull()) {
        m_canvas->setImage(m_lastEdited);
    } else {
        retone();
    }
}

void RetouchTab::setAdjustments(const Adjustments &a) {
    bool geom = geometryDiffers(a, m_adj);
    m_adj = a;
    if (geom) rebuildGeom();
    else retone();
    markEdited();
}

void RetouchTab::setCropMode(bool on) {
    m_cropMode = on;
    m_canvas->setCropMode(on);
    if (on) m_canvas->setFocus(); // so Enter commits the crop
    else m_canvas->clearSelection();
    m_pendingCrop = QRect();
    emit cropPending(false);
    rebuildGeom(); // show uncropped while selecting
}

void RetouchTab::setCropAspect(double widthOverHeight) {
    m_canvas->setCropAspect(widthOverHeight);
}

void RetouchTab::setWbPickMode(bool on) {
    m_canvas->setPickMode(on);
    if (on) m_canvas->setFocus();
}

void RetouchTab::zoomFit() { m_canvas->zoomFit(); }
void RetouchTab::setZoomPercent(double percent) { m_canvas->setZoomPercent(percent); }
double RetouchTab::zoomPercent() const { return m_canvas->zoomPercent(); }

void RetouchTab::onColorPicked(const QColor &c) {
    // Neutralise the sampled pixel: gains scale each channel to the mean.
    double r = c.red(), g = c.green(), b = c.blue();
    double mean = (r + g + b) / 3.0;
    if (r < 1) r = 1;
    if (g < 1) g = 1;
    if (b < 1) b = 1;
    m_adj.wbR = mean / r;
    m_adj.wbG = mean / g;
    m_adj.wbB = mean / b;
    retone();
    markEdited();
    emit wbPicked();
}

void RetouchTab::onCanvasCrop(const QRect &r) {
    if (r.isEmpty() || m_scaleFromGeom <= 0) {
        m_pendingCrop = QRect();
        emit cropPending(false);
        return;
    }
    double inv = 1.0 / m_scaleFromGeom; // display(scaled) -> geom(full oriented)
    m_pendingCrop = QRect(int(r.x() * inv), int(r.y() * inv),
                          int(r.width() * inv), int(r.height() * inv))
                        .intersected(m_geomImg.rect());
    emit cropPending(!m_pendingCrop.isEmpty());
}

void RetouchTab::applyCrop() {
    if (m_pendingCrop.isEmpty()) return;
    m_adj.cropRect = m_pendingCrop;
    m_pendingCrop = QRect();
    m_cropMode = false;
    m_canvas->setCropMode(false);
    m_canvas->clearSelection();
    emit cropPending(false);
    emit cropModeExited();
    rebuildGeom();
    markEdited();
}

void RetouchTab::resetCrop() {
    m_adj.cropRect = QRect();
    m_pendingCrop = QRect();
    m_canvas->clearSelection();
    emit cropPending(false);
    rebuildGeom();
    markEdited();
}

QImage RetouchTab::renderFullRes() const {
    if (m_base.isNull()) return QImage();
    return applyAdjustments(m_base, m_adj);
}
