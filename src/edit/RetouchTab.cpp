#include "edit/RetouchTab.h"
#include "edit/ImageCanvas.h"
#include "edit/RawLoader.h"
#include "edit/EditSidecar.h"
#include "edit/HealTool.h"

#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>

void RenderWorker::render(const QImage &src, const Adjustments &adj, int maskSnapshotIndex) {
    QImage maskSnapshot;
    QImage result = applyAdjustments(src, adj, &m_brushCache, maskSnapshotIndex,
                                     maskSnapshotIndex >= 0 ? &maskSnapshot : nullptr);
    emit done(result, maskSnapshot);
}

namespace {
constexpr int kDisplayMaxDim = 1600; // interactive preview resolution cap

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
    // Any image layers restored from the sidecar need their source photo
    // decoded again — the cache is never persisted.
    for (const Mask &m : m_adj.masks)
        if (m.isImageLayer()) kickoffImageLayerDecode(m.sourceImagePath);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new ImageCanvas;
    layout->addWidget(m_canvas);
    connect(m_canvas, &ImageCanvas::cropSelected, this, &RetouchTab::onCanvasCrop);
    connect(m_canvas, &ImageCanvas::commitCropRequested, this, &RetouchTab::applyCrop);
    connect(m_canvas, &ImageCanvas::colorPicked, this, &RetouchTab::onColorPicked);
    connect(m_canvas, &ImageCanvas::colorRangePickStarted, this,
            &RetouchTab::onColorRangePickStarted);
    connect(m_canvas, &ImageCanvas::colorRangeDragged, this,
            &RetouchTab::onColorRangeDragged);
    connect(m_canvas, &ImageCanvas::colorRangeReleased, this,
            &RetouchTab::onColorRangeReleased);
    connect(m_canvas, &ImageCanvas::healAt, this, &RetouchTab::onHealAt);
    connect(m_canvas, &ImageCanvas::eraseAt, this, &RetouchTab::onEraseAt);
    connect(m_canvas, &ImageCanvas::eraseFinished, this, &RetouchTab::onEraseFinished);
    connect(m_canvas, &ImageCanvas::zoomChanged, this, &RetouchTab::zoomChanged);
    connect(m_canvas, &ImageCanvas::healBrushRadiusChanged, this, [this](int r) {
        m_healRadiusDisplay = r; // keep in sync so heal ops use the new size
        emit healBrushChanged(r);
    });
    connect(m_canvas, &ImageCanvas::maskRadialDragged, this, &RetouchTab::onMaskRadial);
    connect(m_canvas, &ImageCanvas::maskLinearDragged, this, &RetouchTab::onMaskLinear);
    connect(m_canvas, &ImageCanvas::maskBrushPoint, this, &RetouchTab::onMaskBrushPoint);
    connect(m_canvas, &ImageCanvas::maskEditFinished, this, &RetouchTab::onMaskEditFinished);
    connect(m_canvas, &ImageCanvas::imageLayerDropped, this, &RetouchTab::addImageLayer);
    connect(m_canvas, &ImageCanvas::maskBrushRadiusChanged, this, [this](double r) {
        if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
        m_adj.masks[m_activeMask].brushRadius = r;
        pushMaskGizmo();
        retone();
        markEdited();
        emit maskBrushChanged(r);
    });
    connect(m_canvas, &ImageCanvas::imageLayerTransformChanged, this,
            [this](const QPointF &offset, const QPointF &scale, bool lockRatio) {
                if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
                Mask &m = m_adj.masks[m_activeMask];
                if (!m.isImageLayer()) return;
                m.sourceImageOffset = offset;
                m.sourceImageScale = scale;
                m.sourceImageLockRatio = lockRatio;
                retone();
                markEdited();
            });
    connect(m_canvas, &ImageCanvas::backgroundColorChanged, this,
            [this](const QColor &color) {
                if (m_adj.backgroundColor == color) return;
                m_adj.backgroundColor = color;
                markEdited();
            });

    m_canvas->setBackgroundColor(m_adj.backgroundColor); // restored from sidecar, if any
    m_canvas->setPlaceholder("Decoding RAW…");

    // After dragging stops, upgrade the preview with the expensive convolutions.
    m_fullRenderTimer = new QTimer(this);
    m_fullRenderTimer->setSingleShot(true);
    m_fullRenderTimer->setInterval(140);
    connect(m_fullRenderTimer, &QTimer::timeout, this, &RetouchTab::retoneFull);

    // Coalesce rapid edits (a slider drag) into one undo step.
    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(350);
    connect(m_commitTimer, &QTimer::timeout, this, &RetouchTab::commitHistory);

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
    // Seed the undo history with the initial (loaded) state.
    m_history = {m_adj};
    m_histIndex = 0;
    emit historyChanged(false, false);
    emit historyListChanged();
}

bool RetouchTab::hasEdits() const {
    return hasToneEdits(m_adj) || m_adj.rotationQuadrants != 0 || m_adj.flipH ||
           m_adj.flipV || !m_adj.cropRect.isNull() || !m_adj.heals.isEmpty();
}

void RetouchTab::markEdited() {
    m_dirty = true;
    emit editStateChanged(true, hasEdits());
    if (m_commitTimer) m_commitTimer->start(); // schedule an undo snapshot
}

void RetouchTab::commitHistory() {
    if (m_histIndex < 0) { // not seeded yet
        m_history = {m_adj};
        m_histIndex = 0;
        emit historyChanged(canUndo(), canRedo());
        emit historyListChanged();
        return;
    }
    if (m_adj == m_history[m_histIndex]) return; // nothing new to record
    m_history.resize(m_histIndex + 1);           // drop any redo branch
    m_history.append(m_adj);
    m_histIndex = m_history.size() - 1;
    const int kMaxHistory = 60;
    if (m_history.size() > kMaxHistory) {
        m_history.removeFirst();
        --m_histIndex;
    }
    emit historyChanged(canUndo(), canRedo());
    emit historyListChanged();
}

void RetouchTab::applyHistoryState() {
    m_adj = m_history[m_histIndex];
    rebuildGeom();
    m_canvas->setBackgroundColor(m_adj.backgroundColor);
    // Keep the active-mask index valid after undo/redo changes the mask list.
    if (m_activeMask >= m_adj.masks.size())
        m_activeMask = m_adj.masks.isEmpty() ? -1 : m_adj.masks.size() - 1;
    pushMaskGizmo();
    m_dirty = true;
    emit adjustmentsReplaced();
    emit masksChanged();
    emit editStateChanged(m_dirty, hasEdits());
    emit historyChanged(canUndo(), canRedo());
    emit historyListChanged();
}

void RetouchTab::undo() {
    if (m_commitTimer) m_commitTimer->stop();
    commitHistory(); // capture any in-progress change first
    if (!canUndo()) return;
    --m_histIndex;
    applyHistoryState();
}

void RetouchTab::redo() {
    if (m_commitTimer) m_commitTimer->stop();
    commitHistory();
    if (!canRedo()) return;
    ++m_histIndex;
    applyHistoryState();
}

void RetouchTab::jumpToHistory(int index) {
    if (m_commitTimer) m_commitTimer->stop();
    commitHistory(); // capture any in-progress change first
    if (m_history.isEmpty()) return;
    index = qBound(0, index, m_history.size() - 1);
    if (index == m_histIndex) return;
    m_histIndex = index;
    applyHistoryState();
}

void RetouchTab::saveEdits() {
    EditSidecar::save(m_path, m_adj);
    // Cache the edited look so the filmstrip reflects it across sessions.
    if (!m_lastEdited.isNull())
        EditSidecar::saveThumbnail(m_path, m_lastEdited);
    m_dirty = false;
    emit editStateChanged(false, hasEdits());
}

void RetouchTab::rebuildGeom() {
    if (m_base.isNull()) return;
    // Orient (no crop) → heal in oriented space → crop. Healing before crop
    // keeps heal coordinates independent of the crop rectangle.
    Adjustments orientAdj;
    orientAdj.rotationQuadrants = m_adj.rotationQuadrants;
    orientAdj.flipH = m_adj.flipH;
    orientAdj.flipV = m_adj.flipV;
    QImage oriented = applyAdjustments(m_base, orientAdj);
    if (!m_adj.heals.isEmpty()) applyHeal(oriented, m_adj.heals);

    m_geomImg = oriented;
    if (!m_cropMode && !m_adj.cropRect.isNull()) {
        QRect r = m_adj.cropRect.intersected(oriented.rect());
        if (r.isValid() && !r.isEmpty()) m_geomImg = oriented.copy(r);
    }

    if (qMax(m_geomImg.width(), m_geomImg.height()) > kDisplayMaxDim) {
        m_scaled = m_geomImg.scaled(kDisplayMaxDim, kDisplayMaxDim,
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        m_scaled = m_geomImg;
    }
    m_scaleFromGeom = m_geomImg.width() > 0
                          ? double(m_scaled.width()) / m_geomImg.width()
                          : 1.0;
    updateHealSpots();
    retone();
}

// Convert stored heal ops (oriented-image, pre-crop coords) into the display
// (m_scaled) pixel space so the canvas can draw hover-highlight markers.
void RetouchTab::updateHealSpots() {
    QVector<ImageCanvas::HealMarker> spots;
    QPoint offset = (!m_cropMode && !m_adj.cropRect.isNull())
                        ? m_adj.cropRect.topLeft() : QPoint(0, 0);
    for (const HealOp &op : m_adj.heals) {
        ImageCanvas::HealMarker m;
        m.pos = QPointF((op.x - offset.x()) * m_scaleFromGeom,
                         (op.y - offset.y()) * m_scaleFromGeom);
        m.radius = op.radius * m_scaleFromGeom;
        spots.append(m);
    }
    m_canvas->setHealSpots(spots);
}

void RetouchTab::retone() {
    if (m_scaled.isNull()) return;
    // Fast interactive path: skip the clarity/sharpen blur convolutions (the
    // expensive part) while the user is dragging, then schedule a full render.
    Adjustments t = toneOnly(m_adj);
    bool heavy = (t.denoise != 0 || t.clarity != 0 || t.sharpen != 0);
    if (heavy) { t.denoise = 0; t.clarity = 0; t.sharpen = 0; }
    requestRender(m_scaled, t, maskPreviewIndex());
    if (heavy) m_fullRenderTimer->start();
    else m_fullRenderTimer->stop();
}

void RetouchTab::retoneFull() {
    if (m_scaled.isNull()) return;
    requestRender(m_scaled, toneOnly(m_adj), maskPreviewIndex());
}

// Coalesced async render: at most one job in flight; newer requests overwrite
// the pending one so intermediate drag frames are dropped, not queued.
void RetouchTab::requestRender(const QImage &src, const Adjustments &adj, int maskSnapshotIndex) {
    if (m_rendering) {
        m_pendingSrc = src;
        m_pendingAdj = adj;
        m_pendingMaskIdx = maskSnapshotIndex;
        m_hasPending = true;
        return;
    }
    m_rendering = true;
    QMetaObject::invokeMethod(m_renderWorker, "render", Qt::QueuedConnection,
                              Q_ARG(QImage, src), Q_ARG(Adjustments, adj),
                              Q_ARG(int, maskSnapshotIndex));
}

void RetouchTab::onRenderDone(const QImage &result, const QImage &maskSnapshot) {
    m_lastEdited = result;
    if (!maskSnapshot.isNull()) m_maskPreviewImage = maskSnapshot;
    if (!m_showingOriginal) m_canvas->setImage(result);
    emit previewUpdated();
    if (m_maskPreviewEnabled) emit maskPreviewUpdated();
    m_rendering = false;
    if (m_hasPending) {
        m_hasPending = false;
        requestRender(m_pendingSrc, m_pendingAdj, m_pendingMaskIdx);
    }
}

void RetouchTab::setMaskPreviewEnabled(bool on) {
    if (m_maskPreviewEnabled == on) return;
    m_maskPreviewEnabled = on;
    if (on) retone(); // populate the snapshot for the currently active layer
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
    Adjustments merged = a;
    // Preserve the runtime-only image-layer decode cache: callers snapshot
    // adjustments(), mutate one field, and write the whole struct back, which
    // would otherwise silently revert a cache populated by an in-flight
    // kickoffImageLayerDecode() and permanently disable that layer.
    for (int i = 0; i < merged.masks.size() && i < m_adj.masks.size(); ++i) {
        if (merged.masks[i].sourceImagePath == m_adj.masks[i].sourceImagePath) {
            merged.masks[i].sourceImageCache = m_adj.masks[i].sourceImageCache;
            merged.masks[i].sourceMissing = m_adj.masks[i].sourceMissing;
        }
    }
    m_adj = merged;
    pushMaskGizmo();
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

void RetouchTab::setColorRangePickMode(bool on) {
    m_canvas->setColorRangePickMode(on);
    if (on) m_canvas->setFocus();
}

// A targeted color-range pick started: reuse a close-enough existing entry
// (same dominant channel) so re-picking the same color continues adjusting it,
// otherwise append a fresh entry for this gesture.
void RetouchTab::onColorRangePickStarted(const QColor &c) {
    const int channel = (c.green() >= c.red() && c.green() >= c.blue())
                            ? 1
                            : (c.red() >= c.blue() ? 0 : 2);
    m_crIndex = -1;
    for (int i = 0; i < m_adj.colorRanges.size(); ++i) {
        const ColorRangeAdjust &cr = m_adj.colorRanges[i];
        if (cr.channel != channel) continue;
        const int dr = cr.r - c.red(), dg = cr.g - c.green(), db = cr.b - c.blue();
        if (dr * dr + dg * dg + db * db <= 30 * 30) {
            m_crIndex = i;
            break;
        }
    }
    if (m_crIndex < 0) {
        ColorRangeAdjust cr;
        cr.r = c.red();
        cr.g = c.green();
        cr.b = c.blue();
        cr.channel = channel;
        m_adj.colorRanges.append(cr);
        m_crIndex = m_adj.colorRanges.size() - 1;
    }
    m_crBaseAmount = m_adj.colorRanges[m_crIndex].amount;
}

void RetouchTab::onColorRangeDragged(int dxPixels) {
    if (m_crIndex < 0 || m_crIndex >= m_adj.colorRanges.size()) return;
    const int amount = qBound(-100, m_crBaseAmount + dxPixels / 3, 100);
    if (m_adj.colorRanges[m_crIndex].amount == amount) return;
    m_adj.colorRanges[m_crIndex].amount = amount;
    m_canvas->setColorRangeAmount(amount);
    retone();
}

void RetouchTab::onColorRangeReleased() {
    if (m_crIndex < 0 || m_crIndex >= m_adj.colorRanges.size()) return;
    const bool removed = m_adj.colorRanges[m_crIndex].amount == 0;
    if (removed) m_adj.colorRanges.removeAt(m_crIndex);
    m_crIndex = -1;
    if (removed) retone();
    markEdited();
}

void RetouchTab::setHealMode(bool on) {
    m_canvas->setHealMode(on);
    if (on) m_canvas->setFocus();
}

void RetouchTab::setZoomMode(bool on) {
    m_canvas->setZoomMode(on);
    if (on) m_canvas->setFocus();
}

void RetouchTab::setHealBrush(int radiusDisplayPx) {
    m_healRadiusDisplay = radiusDisplayPx;
    m_canvas->setBrushRadius(radiusDisplayPx);
}

void RetouchTab::setEraseMode(bool on) {
    m_canvas->setEraseMode(on);
    if (on) m_canvas->setFocus();
}

void RetouchTab::setEraseBrush(int radiusDisplayPx) {
    m_eraseRadiusDisplay = radiusDisplayPx;
    m_canvas->setBrushRadius(radiusDisplayPx);
}

void RetouchTab::clearHeals() {
    if (m_adj.heals.isEmpty()) return;
    m_adj.heals.clear();
    rebuildGeom();
    markEdited();
}

// A heal spot was placed on the canvas (point in display-image coords).
void RetouchTab::onHealAt(const QPoint &imgPoint) {
    if (m_scaleFromGeom <= 0 || m_geomImg.isNull()) return;
    double inv = 1.0 / m_scaleFromGeom; // display(scaled) -> geom(full, cropped)
    // Convert cropped-geom coords to oriented coords (heals live pre-crop).
    QPoint offset = (!m_cropMode && !m_adj.cropRect.isNull())
                        ? m_adj.cropRect.topLeft() : QPoint(0, 0);
    HealOp op;
    op.x = int(imgPoint.x() * inv) + offset.x();
    op.y = int(imgPoint.y() * inv) + offset.y();
    op.radius = qMax(2, int(m_healRadiusDisplay * inv));
    m_adj.heals.append(op);
    rebuildGeom();
    markEdited();
}

// An erase dab was placed on the canvas (point in display-image, width-
// normalized coords — same space as onMaskBrushPoint). Only image layers
// can be erased; ImageCanvas already gates this on m_hasActiveImageLayer,
// but the active layer can still be non-image if selection changed
// mid-drag, so re-check here too.
void RetouchTab::onEraseAt(const QPointF &ptNorm) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (!m.isImageLayer()) return;
    double radiusNorm = (m_scaled.isNull() || m_scaled.width() <= 0)
                             ? 0.06
                             : m_eraseRadiusDisplay / double(m_scaled.width());
    m.eraseStrokes.append(ErasePoint{ptNorm, radiusNorm});
    retone();
}

void RetouchTab::onEraseFinished() {
    markEdited(); // schedule one coalesced undo step for the whole drag
}

// ---- Local adjustment masks ------------------------------------------------

void RetouchTab::pushMaskGizmo() {
    if (m_activeMask >= 0 && m_activeMask < m_adj.masks.size()) {
        const Mask &m = m_adj.masks[m_activeMask];
        m_canvas->setMaskMode(m.type, m_maskMode);
        m_canvas->setActiveMask(true, m);
    } else {
        m_canvas->setActiveMask(false, Mask{});
    }
}

void RetouchTab::setMaskMode(bool on) {
    m_maskMode = on;
    // Enable the canvas for the active mask's type (default Radial if none).
    MaskType kind = (m_activeMask >= 0 && m_activeMask < m_adj.masks.size())
                        ? m_adj.masks[m_activeMask].type
                        : MaskType::Radial;
    m_canvas->setMaskMode(kind, on);
    pushMaskGizmo();
    if (on) m_canvas->setFocus();
}

int RetouchTab::addMask(MaskType type) {
    Mask m;
    m.type = type;
    m.name = QStringLiteral("Layer %1").arg(m_adj.masks.size() + 1);
    m_adj.masks.append(m);
    m_activeMask = m_adj.masks.size() - 1;
    m_maskMode = (type != MaskType::None);
    m_canvas->setMaskMode(type, m_maskMode);
    pushMaskGizmo();
    if (m_maskPreviewEnabled) retone();
    markEdited();
    emit masksChanged();
    return m_activeMask;
}

int RetouchTab::addImageLayer(const QString &path) {
    Mask m;
    m.type = MaskType::None; // covers the full frame; no shape
    m.name = QFileInfo(path).fileName();
    m.sourceImagePath = path;
    m.sourceImageOffset = QPointF(0.0, 0.0);
    m.sourceImageScale = QPointF(1.0, 1.0);
    m.sourceImageLockRatio = true;
    m_adj.masks.append(m);
    m_activeMask = m_adj.masks.size() - 1;
    m_maskMode = false;
    m_canvas->setMaskMode(MaskType::None, false);
    pushMaskGizmo();
    if (m_maskPreviewEnabled) retone();
    markEdited();
    emit masksChanged();
    kickoffImageLayerDecode(path);
    return m_activeMask;
}

void RetouchTab::kickoffImageLayerDecode(const QString &path) {
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, path, watcher] {
        QImage img = watcher->result();
        watcher->deleteLater();
        bool changed = false;
        for (Mask &m : m_adj.masks) {
            if (m.sourceImagePath == path && m.sourceImageCache.isNull() && !m.sourceMissing) {
                if (img.isNull()) m.sourceMissing = true;
                else m.sourceImageCache = img;
                changed = true;
            }
        }
        if (changed) {
            retone();
            emit masksChanged();
        }
    });
    watcher->setFuture(QtConcurrent::run(RawLoader::loadAny, path));
}

int RetouchTab::duplicateActiveMask() {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return -1;
    Mask copy = m_adj.masks[m_activeMask];
    copy.name = copy.name.isEmpty() ? QStringLiteral("Layer copy")
                                    : copy.name + QStringLiteral(" copy");
    int insertAt = m_activeMask + 1;
    m_adj.masks.insert(insertAt, copy);
    m_activeMask = insertAt;
    m_maskMode = (copy.type != MaskType::None);
    m_canvas->setMaskMode(copy.type, m_maskMode);
    pushMaskGizmo();
    retone();
    markEdited();
    emit masksChanged();
    return m_activeMask;
}

void RetouchTab::selectMask(int index) {
    if (index < -1 || index >= m_adj.masks.size()) return;
    m_activeMask = index;
    pushMaskGizmo();
    if (m_maskPreviewEnabled) retone();
    emit masksChanged();
}

void RetouchTab::deleteActiveMask() {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    m_adj.masks.remove(m_activeMask);
    m_activeMask = m_adj.masks.isEmpty() ? -1
                                         : qMin(m_activeMask, m_adj.masks.size() - 1);
    pushMaskGizmo();
    retone();
    markEdited();
    emit masksChanged();
}

void RetouchTab::setActiveMaskType(MaskType type) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (m.type == type) return;
    m.type = type;
    m_maskMode = (type != MaskType::None);
    m_canvas->setMaskMode(type, m_maskMode);
    pushMaskGizmo();
    retone();
    markEdited();
    emit masksChanged();
}

void RetouchTab::setActiveMaskAdjust(const MaskAdjust &a) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    if (m_adj.masks[m_activeMask].adj == a) return;
    m_adj.masks[m_activeMask].adj = a;
    retone();
    markEdited();
}

void RetouchTab::setActiveMaskImageTransform(double offsetX, double offsetY,
                                            double scaleX, double scaleY,
                                            bool lockRatio) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (!m.isImageLayer()) return;
    QPointF pos(qBound(-1.0, offsetX, 1.0), qBound(-1.0, offsetY, 1.0));
    QPointF scale(qBound(0.10, scaleX, 3.0), qBound(0.10, scaleY, 3.0));
    if (lockRatio) {
        const double s = std::max(scale.x(), scale.y());
        scale = QPointF(s, s);
    }
    if (m.sourceImageOffset == pos && m.sourceImageScale == scale &&
        m.sourceImageLockRatio == lockRatio)
        return;
    m.sourceImageOffset = pos;
    m.sourceImageScale = scale;
    m.sourceImageLockRatio = lockRatio;
    pushMaskGizmo();
    retone();
    markEdited();
    emit masksChanged();
}

void RetouchTab::setActiveMaskOpacity(double opacity) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    double clamped = std::clamp(opacity, 0.0, 1.0);
    if (std::abs(m.opacity - clamped) < 1e-9) return;
    m.opacity = clamped;
    retone();
    markEdited();
}

void RetouchTab::setActiveMaskBlend(BlendMode mode) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (m.blend == mode) return;
    m.blend = mode;
    retone();
    markEdited();
}

void RetouchTab::setActiveMaskVisible(bool visible) {
    setMaskVisible(m_activeMask, visible);
}

void RetouchTab::setMaskVisible(int index, bool visible) {
    if (index < 0 || index >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[index];
    if (m.visible == visible) return;
    m.visible = visible;
    retone();
    markEdited();
    emit masksChanged();
}

void RetouchTab::setActiveMaskName(const QString &name) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    m_adj.masks[m_activeMask].name = name;
    emit masksChanged();
}

void RetouchTab::moveMask(int from, int to) {
    if (from < 0 || from >= m_adj.masks.size() || to < 0 ||
        to >= m_adj.masks.size() || from == to)
        return;
    m_adj.masks.move(from, to);
    if (m_activeMask == from) m_activeMask = to;
    else if (from < m_activeMask && m_activeMask <= to) --m_activeMask;
    else if (to <= m_activeMask && m_activeMask < from) ++m_activeMask;
    retone();
    markEdited();
    emit masksChanged();
}

void RetouchTab::setActiveMaskShape(bool inverted, double feather,
                                    double hardness, double brushRadius,
                                    bool autoMask) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    m.inverted = inverted;
    m.feather = feather;
    m.hardness = hardness;
    m.brushRadius = brushRadius;
    m.autoMask = autoMask;
    pushMaskGizmo();
    retone();
    markEdited();
}

void RetouchTab::setPaintColor(const QColor &color) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (m.type != MaskType::Paint) return;
    m.paintColor = color;
    retone();
    markEdited();
}

void RetouchTab::onMaskRadial(const QPointF &centerNorm, double radiusNorm) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    m.center = centerNorm;
    m.radiusX = m.radiusY = std::max(0.01, radiusNorm);
    pushMaskGizmo();
    retone();
}

void RetouchTab::onMaskLinear(const QPointF &p0Norm, const QPointF &p1Norm) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    m.p0 = p0Norm;
    m.p1 = p1Norm;
    pushMaskGizmo();
    retone();
}

void RetouchTab::onMaskBrushPoint(const QPointF &ptNorm, bool erase) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    m_adj.masks[m_activeMask].stroke.append(BrushStrokePoint{ptNorm, erase});
    pushMaskGizmo(); // show the painted coverage right away
    retone();
}

void RetouchTab::onMaskEditFinished() {
    // Once a brush stroke is committed, hide its overlay/gizmo again so it
    // doesn't sit on top of the image; it reappears while actively painting
    // (onMaskBrushPoint) or when explicitly reselected from the mask list.
    if (m_activeMask >= 0 && m_activeMask < m_adj.masks.size() &&
        m_adj.masks[m_activeMask].type == MaskType::Brush) {
        m_canvas->setActiveMask(false, Mask{});
    }
    markEdited(); // schedule one coalesced undo step for the whole drag
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
    if (m_geomImg.isNull()) return QImage();
    // m_geomImg is the full-res oriented + healed + cropped base; apply tone.
    return applyAdjustments(m_geomImg, toneOnly(m_adj));
}
