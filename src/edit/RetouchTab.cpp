#include "edit/RetouchTab.h"
#include "edit/ImageCanvas.h"
#include "edit/RawLoader.h"
#include "edit/EditSidecar.h"
#include "edit/HealTool.h"
#include "edit/TextTool.h"
#include "edit/ShapeTool.h"

#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <QFont>
#include <algorithm>
#include <cmath>

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

    setupCanvasAndWiring();

    // Decode off the GUI thread.
    m_watcher = new QFutureWatcher<QImage>(this);
    connect(m_watcher, &QFutureWatcher<QImage>::finished, this,
            &RetouchTab::onDecodeFinished);
    m_watcher->setFuture(QtConcurrent::run(RawLoader::load, m_path));
}

RetouchTab::RetouchTab(const QSize &blankSize, QWidget *parent)
    : QWidget(parent), m_path(QString()) {
    m_base = QImage(blankSize, QImage::Format_ARGB32);
    m_base.fill(Qt::transparent);

    setupCanvasAndWiring();

    rebuildGeom();
    retone();
    emit decoded(true);
}

void RetouchTab::setupCanvasAndWiring() {
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
    connect(m_canvas, &ImageCanvas::textPlaceRequested, this, &RetouchTab::onTextPlaceRequested);
    connect(m_canvas, &ImageCanvas::textSelected, this, &RetouchTab::onTextSelected);
    connect(m_canvas, &ImageCanvas::textDeselected, this, &RetouchTab::onTextDeselected);
    connect(m_canvas, &ImageCanvas::textMoved, this, &RetouchTab::onTextMoved);
    connect(m_canvas, &ImageCanvas::textRotated, this, &RetouchTab::onTextRotated);
    connect(m_canvas, &ImageCanvas::textEditRequested, this, &RetouchTab::onTextEditRequested);
    connect(m_canvas, &ImageCanvas::textEditCommitted, this, &RetouchTab::onTextEditCommitted);
    connect(m_canvas, &ImageCanvas::textEditCancelled, this, &RetouchTab::onTextEditCancelled);
    connect(m_canvas, &ImageCanvas::textLiveContentChanged, this,
            &RetouchTab::onTextLiveContentChanged);
    connect(m_canvas, &ImageCanvas::textDeleteRequested, this, &RetouchTab::onTextDeleteRequested);
    connect(m_canvas, &ImageCanvas::textResizeStarted, this, &RetouchTab::onTextResizeStarted);
    connect(m_canvas, &ImageCanvas::textResized, this, &RetouchTab::onTextResized);
    connect(m_canvas, &ImageCanvas::shapeCreateRequested, this, &RetouchTab::onShapeCreateRequested);
    connect(m_canvas, &ImageCanvas::shapeSelected, this, &RetouchTab::onShapeSelected);
    connect(m_canvas, &ImageCanvas::shapeDeselected, this, &RetouchTab::onShapeDeselected);
    connect(m_canvas, &ImageCanvas::shapeMoved, this, &RetouchTab::onShapeMoved);
    connect(m_canvas, &ImageCanvas::shapeResized, this, &RetouchTab::onShapeResized);
    connect(m_canvas, &ImageCanvas::shapeLineEndpointsChanged, this,
            &RetouchTab::onShapeLineEndpointsChanged);
    connect(m_canvas, &ImageCanvas::shapeRotated, this, &RetouchTab::onShapeRotated);
    connect(m_canvas, &ImageCanvas::shapeDeleteRequested, this, &RetouchTab::onShapeDeleteRequested);
    connect(m_canvas, &ImageCanvas::shapeDuplicateRequested, this,
            &RetouchTab::onShapeDuplicateRequested);
    connect(m_canvas, &ImageCanvas::shapeGroupDuplicateRequested, this,
            &RetouchTab::onShapeGroupDuplicateRequested);
    connect(m_canvas, &ImageCanvas::shapeRaiseRequested, this, &RetouchTab::onShapeRaiseRequested);
    connect(m_canvas, &ImageCanvas::shapeLowerRequested, this, &RetouchTab::onShapeLowerRequested);
    connect(m_canvas, &ImageCanvas::shapeGroupDeleteRequested, this,
            &RetouchTab::onShapeGroupDeleteRequested);
    connect(m_canvas, &ImageCanvas::shapeToggleSelectRequested, this,
            &RetouchTab::onShapeToggleSelectRequested);
    connect(m_canvas, &ImageCanvas::shapeGroupMoveStarted, this,
            &RetouchTab::onShapeGroupMoveStarted);
    connect(m_canvas, &ImageCanvas::shapeGroupMoveRequested, this,
            &RetouchTab::onShapeGroupMoveRequested);
    connect(m_canvas, &ImageCanvas::shapeGroupResizeStarted, this,
            &RetouchTab::onShapeGroupMoveStarted);
    connect(m_canvas, &ImageCanvas::shapeGroupResizeRequested, this,
            &RetouchTab::onShapeGroupResizeRequested);
    connect(m_canvas, &ImageCanvas::eraseAt, this, &RetouchTab::onEraseAt);
    connect(m_canvas, &ImageCanvas::eraseFinished, this, &RetouchTab::onEraseFinished);
    connect(m_canvas, &ImageCanvas::zoomChanged, this, &RetouchTab::zoomChanged);
    connect(m_canvas, &ImageCanvas::healBrushRadiusChanged, this, [this](int r) {
        m_healRadiusDisplay = r; // keep in sync so heal ops use the new size
        emit healBrushChanged(r);
    });
    connect(m_canvas, &ImageCanvas::eraseBrushRadiusChanged, this, [this](int r) {
        m_eraseRadiusDisplay = r; // keep in sync so erase dabs use the new size
        emit eraseBrushChanged(r);
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
           m_adj.flipV || !m_adj.cropRect.isNull() || !m_adj.heals.isEmpty() ||
           !m_adj.texts.isEmpty() || !m_adj.shapes.isEmpty();
}

void RetouchTab::assignPath(const QString &path) {
    m_path = path;
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
    m_geomCropOffset = QPoint();
    if (!m_cropMode && !m_adj.cropRect.isNull()) {
        QRect r = m_adj.cropRect.intersected(oriented.rect());
        if (r.isValid() && !r.isEmpty()) {
            m_geomImg = oriented.copy(r);
            m_geomCropOffset = r.topLeft();
        }
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
    updateTextMarkers();
    updateShapeMarkers();
    retone();
}

// Convert stored text ops (oriented-image, pre-crop coords) into the display
// (m_scaled) pixel space so the canvas can draw/hit-test selection boxes.
void RetouchTab::updateTextMarkers() {
    QVector<ImageCanvas::TextMarker> markers;
    for (const TextOp &op : m_adj.texts) {
        TextOp local = op;
        local.pos = (op.pos - QPointF(m_geomCropOffset)) * m_scaleFromGeom;
        local.pixelSize *= m_scaleFromGeom;
        ImageCanvas::TextMarker m;
        m.rect = textOpBounds(local);
        m.rotation = op.rotation;
        markers.append(m);
    }
    m_canvas->setTextMarkers(markers);
    m_canvas->setActiveTextIndex(m_activeText);
}

// Convert stored shape ops (oriented-image, pre-crop coords) into the
// display (m_scaled) pixel space so the canvas can draw/hit-test selection
// boxes/handles.
void RetouchTab::updateShapeMarkers() {
    QVector<ImageCanvas::ShapeMarker> markers;
    for (const ShapeOp &op : m_adj.shapes) {
        ImageCanvas::ShapeMarker m;
        m.type = op.type;
        m.rect = QRectF((op.rect.topLeft() - QPointF(m_geomCropOffset)) * m_scaleFromGeom,
                        op.rect.size() * m_scaleFromGeom);
        m.p1 = (op.p1 - QPointF(m_geomCropOffset)) * m_scaleFromGeom;
        m.p2 = (op.p2 - QPointF(m_geomCropOffset)) * m_scaleFromGeom;
        m.rotation = op.rotation;
        markers.append(m);
    }
    m_canvas->setShapeMarkers(markers);
    m_canvas->setActiveShapeIndex(m_activeShape);
    m_canvas->setSelectedShapeIndices(m_selectedShapes);
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
    if (!m_adj.texts.isEmpty()) {
        // The op currently open in the inline editor still composites
        // normally — background/shadow/outline all preview live — but its
        // glyph *fill* is hidden, since the editor widget already shows that
        // live text itself; compositing the fill too would double it up.
        if (m_textEditIndex >= 0 && m_textEditIndex < m_adj.texts.size()) {
            QVector<TextOp> texts = m_adj.texts;
            texts[m_textEditIndex].color.setAlpha(0);
            applyTexts(m_lastEdited, texts, m_geomCropOffset, m_scaleFromGeom);
        } else {
            applyTexts(m_lastEdited, m_adj.texts, m_geomCropOffset, m_scaleFromGeom);
        }
    }
    if (!m_adj.shapes.isEmpty())
        applyShapes(m_lastEdited, m_adj.shapes, m_geomCropOffset, m_scaleFromGeom);
    if (!maskSnapshot.isNull()) m_maskPreviewImage = maskSnapshot;
    if (!m_showingOriginal) m_canvas->setImage(m_lastEdited);
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

void RetouchTab::setTextMode(bool on) {
    m_textMode = on;
    m_canvas->setTextMode(on);
    if (on) m_canvas->setFocus();
    else m_activeText = -1;
}

// A click placed a new text op (point in display-image coords, pre-crop
// stored per TextOp convention). Opens the inline editor immediately;
// committing empty text (or cancelling) discards the draft.
void RetouchTab::onTextPlaceRequested(const QPoint &imgPoint) {
    if (m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    TextOp op = m_textDefaults;
    op.pos = QPointF(imgPoint.x() * inv, imgPoint.y() * inv) + QPointF(m_geomCropOffset);
    op.text.clear();
    m_adj.texts.append(op);
    m_activeText = m_adj.texts.size() - 1;
    m_newTextIndex = m_activeText;
    m_textEditIndex = m_activeText;
    updateTextMarkers();
    retone(); // suppress the (empty) baked-in text while the editor owns it

    QFont font(op.family);
    font.setPixelSize(std::max(1, int(std::lround(op.pixelSize * m_scaleFromGeom))));
    font.setBold(op.bold);
    font.setItalic(op.italic);
    m_canvas->beginTextEdit(m_activeText, QPointF(imgPoint), font, op.color, QString());
    emit textsChanged();
}

void RetouchTab::onTextSelected(int index) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_activeText = index;
    m_newTextIndex = -1;
    m_canvas->setActiveTextIndex(index);
    emit textsChanged();
}

void RetouchTab::onTextDeselected() {
    m_activeText = -1;
    emit textsChanged();
}

void RetouchTab::onTextMoved(int index, const QPointF &newImgPos) {
    if (index < 0 || index >= m_adj.texts.size() || m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    m_adj.texts[index].pos =
        QPointF(newImgPos.x() * inv, newImgPos.y() * inv) + QPointF(m_geomCropOffset);
    updateTextMarkers();
    retone();
    markEdited();
}

void RetouchTab::onTextRotated(int index, double newRotationDegrees) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_adj.texts[index].rotation = newRotationDegrees;
    updateTextMarkers();
    retone();
    markEdited();
}

void RetouchTab::onTextEditRequested(int index) {
    if (index < 0 || index >= m_adj.texts.size() || m_scaleFromGeom <= 0) return;
    m_activeText = index;
    m_newTextIndex = -1;
    m_textEditIndex = index;
    const TextOp &op = m_adj.texts[index];
    QPointF displayPos = (op.pos - QPointF(m_geomCropOffset)) * m_scaleFromGeom;
    QFont font(op.family);
    font.setPixelSize(std::max(1, int(std::lround(op.pixelSize * m_scaleFromGeom))));
    font.setBold(op.bold);
    font.setItalic(op.italic);
    m_canvas->beginTextEdit(index, displayPos, font, op.color, op.text);
    retone(); // suppress the stale baked-in text while the editor owns it
}

void RetouchTab::onTextEditCommitted(int index, const QString &text) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    bool wasNewEmptyDraft = (index == m_newTextIndex) && m_adj.texts[index].text.isEmpty();
    m_newTextIndex = -1;
    if (m_textEditIndex == index) m_textEditIndex = -1;
    if (text.trimmed().isEmpty()) {
        m_adj.texts.removeAt(index);
        if (m_activeText == index) m_activeText = -1;
        else if (m_activeText > index) --m_activeText;
        updateTextMarkers();
        retone();
        if (!wasNewEmptyDraft) markEdited(); // clearing existing text is itself an edit
        return;
    }
    m_adj.texts[index].text = text;
    updateTextMarkers();
    retone();
    markEdited();
}

void RetouchTab::onTextEditCancelled(int index) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    if (m_textEditIndex == index) m_textEditIndex = -1;
    if (index == m_newTextIndex && m_adj.texts[index].text.isEmpty()) {
        m_adj.texts.removeAt(index);
        if (m_activeText == index) m_activeText = -1;
        else if (m_activeText > index) --m_activeText;
        m_newTextIndex = -1;
        updateTextMarkers();
        retone();
    } else {
        retone(); // re-composite the (unchanged) baked-in text now that the editor is gone
    }
}

// Live-updates the op's text as the user types (before committing), so
// shadow/outline/background style controls preview correctly while the
// editor is still open — see onRenderDone's fill-only suppression.
void RetouchTab::onTextLiveContentChanged(int index, const QString &text) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_adj.texts[index].text = text;
    updateTextMarkers();
    retone();
}

void RetouchTab::onTextDeleteRequested(int index) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_adj.texts.removeAt(index);
    if (m_activeText == index) m_activeText = -1;
    else if (m_activeText > index) --m_activeText;
    if (m_newTextIndex == index) m_newTextIndex = -1;
    updateTextMarkers();
    retone();
    markEdited();
}

void RetouchTab::onTextResizeStarted(int index) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_textResizeStartPixelSize = m_adj.texts[index].pixelSize;
}

// Corner-drag resize: uniformly scales font size relative to its value when
// the drag began (`ratio` is cumulative from drag start, not incremental).
void RetouchTab::onTextResized(int index, double ratio) {
    if (index < 0 || index >= m_adj.texts.size()) return;
    m_adj.texts[index].pixelSize = std::clamp(m_textResizeStartPixelSize * ratio, 1.0, 2000.0);
    updateTextMarkers();
    retone();
    markEdited();
    emit textsChanged();
}

void RetouchTab::deleteActiveText() {
    if (m_activeText >= 0) onTextDeleteRequested(m_activeText);
}

TextOp RetouchTab::activeTextStyle() const {
    if (m_activeText >= 0 && m_activeText < m_adj.texts.size()) return m_adj.texts[m_activeText];
    return m_textDefaults;
}

void RetouchTab::setTextFont(const QString &family, double pixelSize, bool bold, bool italic) {
    TextOp *target = (m_activeText >= 0 && m_activeText < m_adj.texts.size())
                         ? &m_adj.texts[m_activeText] : &m_textDefaults;
    target->family = family;
    target->pixelSize = pixelSize;
    target->bold = bold;
    target->italic = italic;
    if (target != &m_textDefaults) {
        updateTextMarkers();
        retone();
        markEdited();
    }
    emit textsChanged();
}

void RetouchTab::setTextColor(const QColor &color) {
    TextOp *target = (m_activeText >= 0 && m_activeText < m_adj.texts.size())
                         ? &m_adj.texts[m_activeText] : &m_textDefaults;
    target->color = color;
    if (target != &m_textDefaults) { retone(); markEdited(); }
    emit textsChanged();
}

void RetouchTab::setTextOutline(bool enabled, const QColor &color, double width) {
    TextOp *target = (m_activeText >= 0 && m_activeText < m_adj.texts.size())
                         ? &m_adj.texts[m_activeText] : &m_textDefaults;
    target->outlineEnabled = enabled;
    target->outlineColor = color;
    target->outlineWidth = width;
    if (target != &m_textDefaults) { retone(); markEdited(); }
    emit textsChanged();
}

void RetouchTab::setTextShadow(bool enabled, const QPointF &offset, double blur, double opacity,
                               const QColor &color) {
    TextOp *target = (m_activeText >= 0 && m_activeText < m_adj.texts.size())
                         ? &m_adj.texts[m_activeText] : &m_textDefaults;
    target->shadowEnabled = enabled;
    target->shadowOffset = offset;
    target->shadowBlur = blur;
    target->shadowOpacity = opacity;
    target->shadowColor = color;
    if (target != &m_textDefaults) { retone(); markEdited(); }
    emit textsChanged();
}

void RetouchTab::setTextBackground(bool enabled, const QColor &color, double opacity,
                                   double padding) {
    TextOp *target = (m_activeText >= 0 && m_activeText < m_adj.texts.size())
                         ? &m_adj.texts[m_activeText] : &m_textDefaults;
    target->bgEnabled = enabled;
    target->bgColor = color;
    target->bgOpacity = opacity;
    target->bgPadding = padding;
    if (target != &m_textDefaults) { retone(); markEdited(); }
    emit textsChanged();
}

// ---- Shape tool -------------------------------------------------------

void RetouchTab::setShapeMode(bool on) {
    m_shapeMode = on;
    m_canvas->setShapeMode(on);
    if (on) m_canvas->setFocus();
    else { m_activeShape = -1; m_selectedShapes.clear(); }
}

void RetouchTab::setActiveShapeType(ShapeType t) {
    m_canvas->setActiveShapeType(t);
    if (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size()) {
        m_adj.shapes[m_activeShape].type = t;
        updateShapeMarkers();
        retone();
        markEdited();
    } else {
        m_shapeDefaults.type = t;
    }
    emit shapesChanged();
}

// A drag-create gesture finished (bounding box in display-image coords,
// pre-crop stored per ShapeOp convention).
void RetouchTab::onShapeCreateRequested(ShapeType type, const QRectF &imageRect) {
    if (m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    ShapeOp op = m_shapeDefaults;
    op.type = type;
    if (type == ShapeType::Line) {
        op.p1 = imageRect.topLeft() * inv + QPointF(m_geomCropOffset);
        op.p2 = imageRect.bottomRight() * inv + QPointF(m_geomCropOffset);
    } else {
        op.rect = QRectF(imageRect.topLeft() * inv, imageRect.size() * inv)
                      .translated(QPointF(m_geomCropOffset));
    }
    m_adj.shapes.append(op);
    m_activeShape = m_adj.shapes.size() - 1;
    m_selectedShapes = {m_activeShape};
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// Plain click on a shape not already part of a multi-selection (from the
// canvas): selects it (or its whole group, see selectShape) and captures
// move-drag start geometry for the clicked shape specifically, since
// ImageCanvas re-checks the (possibly just-expanded) selection right after
// this returns and may switch the in-progress drag to a group move instead.
void RetouchTab::onShapeSelected(int index) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    selectShape(index);
    m_shapeMoveStartRect = m_adj.shapes[index].rect;
    m_shapeMoveStartP1 = m_adj.shapes[index].p1;
    m_shapeMoveStartP2 = m_adj.shapes[index].p2;
}

// Select a shape — from the canvas or the Layers panel. If it belongs to a
// group, every shape sharing that groupId is selected too (grouped shapes
// always act as one unit), matching Illustrator/Photoshop group semantics.
void RetouchTab::selectShape(int index) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    const QString groupId = m_adj.shapes[index].groupId;
    if (groupId.isEmpty()) {
        m_selectedShapes = {index};
    } else {
        m_selectedShapes.clear();
        for (int i = 0; i < m_adj.shapes.size(); ++i)
            if (m_adj.shapes[i].groupId == groupId) m_selectedShapes.insert(i);
    }
    m_activeShape = index;
    updateShapeMarkers();
    emit shapesChanged();
}

void RetouchTab::onShapeDeselected() {
    m_activeShape = -1;
    m_selectedShapes.clear();
    updateShapeMarkers();
    emit shapesChanged();
}

// Ctrl+click (no drag) on a shape: toggle its multi-selection membership
// without disturbing the rest of the selection.
void RetouchTab::onShapeToggleSelectRequested(int index) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    if (m_selectedShapes.contains(index)) {
        m_selectedShapes.remove(index);
        if (m_activeShape == index)
            m_activeShape = m_selectedShapes.isEmpty() ? -1 : *m_selectedShapes.constBegin();
    } else {
        m_selectedShapes.insert(index);
        m_activeShape = index;
    }
    updateShapeMarkers();
    emit shapesChanged();
}

// Press on a shape that's already part of a >1-member selection: capture
// every member's current geometry so the upcoming shapeGroupMoveRequested
// deltas (cumulative from this drag's start) can be applied as absolute
// offsets rather than compounding across move events.
// Shared start-capture for both a group move and a group resize (also used
// via shapeGroupResizeStarted — resize needs the same per-shape reference
// geometry, plus stroke width so it can scale proportionally too).
void RetouchTab::onShapeGroupMoveStarted(const QList<int> &indices) {
    m_shapeGroupStartRect.clear();
    m_shapeGroupStartP1.clear();
    m_shapeGroupStartP2.clear();
    m_shapeGroupStartStrokeWidth.clear();
    for (int idx : indices) {
        if (idx < 0 || idx >= m_adj.shapes.size()) continue;
        m_shapeGroupStartRect[idx] = m_adj.shapes[idx].rect;
        m_shapeGroupStartP1[idx] = m_adj.shapes[idx].p1;
        m_shapeGroupStartP2[idx] = m_adj.shapes[idx].p2;
        m_shapeGroupStartStrokeWidth[idx] = m_adj.shapes[idx].strokeWidth;
    }
}

void RetouchTab::onShapeGroupMoveRequested(const QList<int> &indices, const QPointF &deltaImage) {
    if (m_scaleFromGeom <= 0) return;
    QPointF delta = deltaImage / m_scaleFromGeom;
    for (int idx : indices) {
        if (idx < 0 || idx >= m_adj.shapes.size() || !m_shapeGroupStartRect.contains(idx)) continue;
        ShapeOp &op = m_adj.shapes[idx];
        if (op.type == ShapeType::Line) {
            op.p1 = m_shapeGroupStartP1[idx] + delta;
            op.p2 = m_shapeGroupStartP2[idx] + delta;
        } else {
            op.rect = m_shapeGroupStartRect[idx].translated(delta);
        }
    }
    updateShapeMarkers();
    retone();
    markEdited();
}

// `scaleX`/`scaleY` are absolute factors relative to the group's combined
// bounding box at drag start (see shapeGroupResizeStarted → onShapeGroupMoveStarted),
// not incremental — every selected shape's start geometry is scaled about
// the fixed `anchorImage` corner (display-image space, same convention as
// onShapeResized's newImageRect) each call, so results don't compound across
// move events.
void RetouchTab::onShapeGroupResizeRequested(const QList<int> &indices, const QPointF &anchorImage,
                                             double scaleX, double scaleY) {
    if (m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    QPointF anchor = anchorImage * inv + QPointF(m_geomCropOffset);
    double strokeScale = std::sqrt(std::abs(scaleX * scaleY));
    auto scalePoint = [&](const QPointF &p) {
        return QPointF(anchor.x() + (p.x() - anchor.x()) * scaleX,
                       anchor.y() + (p.y() - anchor.y()) * scaleY);
    };
    for (int idx : indices) {
        if (idx < 0 || idx >= m_adj.shapes.size() || !m_shapeGroupStartRect.contains(idx)) continue;
        ShapeOp &op = m_adj.shapes[idx];
        if (op.type == ShapeType::Line) {
            op.p1 = scalePoint(m_shapeGroupStartP1[idx]);
            op.p2 = scalePoint(m_shapeGroupStartP2[idx]);
        } else {
            QRectF startRect = m_shapeGroupStartRect[idx];
            op.rect = QRectF(scalePoint(startRect.topLeft()), scalePoint(startRect.bottomRight()))
                          .normalized();
        }
        op.strokeWidth = std::max(0.0, m_shapeGroupStartStrokeWidth.value(idx, op.strokeWidth) * strokeScale);
    }
    updateShapeMarkers();
    retone();
    markEdited();
}

// `deltaImage` is the total offset from the drag's press point (display-image
// coords), not an incremental step — RetouchTab must apply it against the
// shape's position as of the drag start (captured in onShapeSelected), not
// accumulate it onto the shape's current (already-moved) position.
void RetouchTab::onShapeMoved(int index, const QPointF &deltaImage) {
    if (index < 0 || index >= m_adj.shapes.size() || m_scaleFromGeom <= 0) return;
    QPointF delta = deltaImage / m_scaleFromGeom;
    ShapeOp &op = m_adj.shapes[index];
    if (op.type == ShapeType::Line) {
        op.p1 = m_shapeMoveStartP1 + delta;
        op.p2 = m_shapeMoveStartP2 + delta;
    } else {
        op.rect = m_shapeMoveStartRect.translated(delta);
    }
    updateShapeMarkers();
    retone();
    markEdited();
}

void RetouchTab::onShapeResized(int index, const QRectF &newImageRect) {
    if (index < 0 || index >= m_adj.shapes.size() || m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    m_adj.shapes[index].rect = QRectF(newImageRect.topLeft() * inv + QPointF(m_geomCropOffset),
                                      newImageRect.size() * inv);
    updateShapeMarkers();
    retone();
    markEdited();
}

void RetouchTab::onShapeLineEndpointsChanged(int index, const QPointF &p1, const QPointF &p2) {
    if (index < 0 || index >= m_adj.shapes.size() || m_scaleFromGeom <= 0) return;
    double inv = 1.0 / m_scaleFromGeom;
    m_adj.shapes[index].p1 = p1 * inv + QPointF(m_geomCropOffset);
    m_adj.shapes[index].p2 = p2 * inv + QPointF(m_geomCropOffset);
    updateShapeMarkers();
    retone();
    markEdited();
}

void RetouchTab::onShapeRotated(int index, double newRotationDegrees) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    m_adj.shapes[index].rotation = newRotationDegrees;
    updateShapeMarkers();
    retone();
    markEdited();
}

void RetouchTab::onShapeDeleteRequested(int index) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    m_adj.shapes.removeAt(index);
    if (m_activeShape == index) m_activeShape = -1;
    else if (m_activeShape > index) --m_activeShape;
    QSet<int> reselected;
    for (int idx : m_selectedShapes) {
        if (idx == index) continue;
        reselected.insert(idx > index ? idx - 1 : idx);
    }
    m_selectedShapes = reselected;
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// Delete/Backspace with more than one shape selected: remove every selected
// shape in one step (highest index first, so earlier removals don't shift
// the indices of shapes still to be removed).
void RetouchTab::onShapeGroupDeleteRequested(const QList<int> &indices) {
    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int idx : sorted) {
        if (idx < 0 || idx >= m_adj.shapes.size()) continue;
        m_adj.shapes.removeAt(idx);
    }
    m_activeShape = -1;
    m_selectedShapes.clear();
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// Ctrl+drag on an existing shape: append an exact copy (same geometry and
// style) and select it, so ImageCanvas's already-in-progress move drag
// continues by dragging the copy away from the untouched original.
void RetouchTab::onShapeDuplicateRequested(int index) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    ShapeOp copy = m_adj.shapes[index];
    copy.groupId.clear(); // a lone duplicate leaves its group, even if the original had one
    m_adj.shapes.append(copy);
    m_activeShape = m_adj.shapes.size() - 1;
    m_selectedShapes = {m_activeShape};
    m_shapeMoveStartRect = copy.rect;
    m_shapeMoveStartP1 = copy.p1;
    m_shapeMoveStartP2 = copy.p2;
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// Ctrl+drag with a multi-selection: append an exact copy of every selected
// shape, select the copies as the new group, and capture their (identical
// to the originals) start geometry so the in-progress group-move drag
// continues by dragging the copies away — the originals are left untouched.
void RetouchTab::onShapeGroupDuplicateRequested(const QList<int> &indices) {
    m_shapeGroupStartRect.clear();
    m_shapeGroupStartP1.clear();
    m_shapeGroupStartP2.clear();

    // If every duplicated shape shares the same (non-empty) group, the
    // copies form their own new group too, preserving that structure —
    // otherwise (an ad-hoc multi-selection) the copies stay ungrouped.
    QString sourceGroupId = indices.isEmpty() || indices.first() < 0 ||
                                    indices.first() >= m_adj.shapes.size()
                                ? QString()
                                : m_adj.shapes[indices.first()].groupId;
    bool sameGroup = !sourceGroupId.isEmpty();
    for (int idx : indices)
        if (idx < 0 || idx >= m_adj.shapes.size() || m_adj.shapes[idx].groupId != sourceGroupId)
            sameGroup = false;
    QString newGroupId = sameGroup ? QUuid::createUuid().toString() : QString();

    QList<int> newIndices;
    for (int idx : indices) {
        if (idx < 0 || idx >= m_adj.shapes.size()) continue;
        ShapeOp copy = m_adj.shapes[idx];
        copy.groupId = newGroupId;
        m_adj.shapes.append(copy);
        int newIdx = m_adj.shapes.size() - 1;
        newIndices.append(newIdx);
        m_shapeGroupStartRect[newIdx] = copy.rect;
        m_shapeGroupStartP1[newIdx] = copy.p1;
        m_shapeGroupStartP2[newIdx] = copy.p2;
    }
    m_selectedShapes = QSet<int>(newIndices.begin(), newIndices.end());
    m_activeShape = newIndices.isEmpty() ? -1 : newIndices.last();
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// '+': move one level toward the top of the stack (shapes render in vector
// order, so "up" means a higher index — swap with the next entry).
void RetouchTab::onShapeRaiseRequested(int index) {
    if (index < 0 || index >= m_adj.shapes.size() - 1) return;
    m_adj.shapes.swapItemsAt(index, index + 1);
    if (m_selectedShapes.remove(index)) m_selectedShapes.insert(index + 1);
    m_activeShape = index + 1;
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// '-': move one level toward the bottom of the stack.
void RetouchTab::onShapeLowerRequested(int index) {
    if (index <= 0 || index >= m_adj.shapes.size()) return;
    m_adj.shapes.swapItemsAt(index, index - 1);
    if (m_selectedShapes.remove(index)) m_selectedShapes.insert(index - 1);
    m_activeShape = index - 1;
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

void RetouchTab::deleteActiveShape() {
    if (m_selectedShapes.size() > 1) onShapeGroupDeleteRequested(m_selectedShapes.values());
    else if (m_activeShape >= 0) onShapeDeleteRequested(m_activeShape);
}

void RetouchTab::setShapeVisible(int index, bool visible) {
    if (index < 0 || index >= m_adj.shapes.size()) return;
    m_adj.shapes[index].visible = visible;
    retone();
    markEdited();
    emit shapesChanged();
}

// Tags the current multi-selection as one group and moves its members to be
// contiguous in the stack (at the position of the topmost/frontmost member,
// so grouping doesn't change what's drawn on top of what), so the group's
// z-order stays a single contiguous block going forward.
void RetouchTab::groupSelectedShapes() {
    if (m_selectedShapes.size() < 2) return;
    QList<int> sorted = m_selectedShapes.values();
    std::sort(sorted.begin(), sorted.end());
    const int originalTop = sorted.last();

    QVector<ShapeOp> members;
    members.reserve(sorted.size());
    for (int i = sorted.size() - 1; i >= 0; --i) {
        members.prepend(m_adj.shapes[sorted[i]]);
        m_adj.shapes.removeAt(sorted[i]);
    }
    const int insertAt = originalTop - (sorted.size() - 1);

    const QString groupId = QUuid::createUuid().toString();
    for (ShapeOp &op : members) op.groupId = groupId;
    for (int i = 0; i < members.size(); ++i) m_adj.shapes.insert(insertAt + i, members[i]);

    m_selectedShapes.clear();
    for (int i = 0; i < members.size(); ++i) m_selectedShapes.insert(insertAt + i);
    m_activeShape = insertAt + members.size() - 1;

    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

// Clears the group tag of every shape sharing a group with the current
// selection — the shapes stay exactly where they are, they just stop
// acting as one unit.
void RetouchTab::ungroupSelectedShapes() {
    QSet<QString> groupIds;
    for (int idx : m_selectedShapes)
        if (idx >= 0 && idx < m_adj.shapes.size() && !m_adj.shapes[idx].groupId.isEmpty())
            groupIds.insert(m_adj.shapes[idx].groupId);
    if (groupIds.isEmpty()) return;
    for (ShapeOp &op : m_adj.shapes)
        if (groupIds.contains(op.groupId)) op.groupId.clear();
    updateShapeMarkers();
    retone();
    markEdited();
    emit shapesChanged();
}

ShapeOp RetouchTab::activeShapeStyle() const {
    if (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size()) return m_adj.shapes[m_activeShape];
    return m_shapeDefaults;
}

void RetouchTab::setShapeSides(int sides) {
    ShapeOp *target = (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size())
                          ? &m_adj.shapes[m_activeShape] : &m_shapeDefaults;
    target->sides = sides;
    if (target != &m_shapeDefaults) { retone(); markEdited(); }
    emit shapesChanged();
}

void RetouchTab::setShapeInnerRadiusRatio(double ratio) {
    ShapeOp *target = (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size())
                          ? &m_adj.shapes[m_activeShape] : &m_shapeDefaults;
    target->innerRadiusRatio = ratio;
    if (target != &m_shapeDefaults) { retone(); markEdited(); }
    emit shapesChanged();
}

void RetouchTab::setShapeFill(bool enabled, const QColor &color) {
    ShapeOp *target = (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size())
                          ? &m_adj.shapes[m_activeShape] : &m_shapeDefaults;
    target->fillEnabled = enabled;
    target->fillColor = color;
    if (target != &m_shapeDefaults) { retone(); markEdited(); }
    emit shapesChanged();
}

void RetouchTab::setShapeStroke(bool enabled, const QColor &color, double width) {
    ShapeOp *target = (m_activeShape >= 0 && m_activeShape < m_adj.shapes.size())
                          ? &m_adj.shapes[m_activeShape] : &m_shapeDefaults;
    target->strokeEnabled = enabled;
    target->strokeColor = color;
    target->strokeWidth = width;
    if (target != &m_shapeDefaults) { retone(); markEdited(); }
    emit shapesChanged();
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
    m_adj.masks.insert(0, m);
    m_activeMask = 0;
    m_maskMode = (type != MaskType::None);
    m_canvas->setMaskMode(type, m_maskMode);
    pushMaskGizmo();
    if (m_maskPreviewEnabled) retone();
    markEdited();
    emit masksChanged();
    return m_activeMask;
}

int RetouchTab::addImageLayer(const QString &path) {
    QString storedPath = copyImageLayerAsset(path);
    Mask m;
    m.type = MaskType::None; // covers the full frame; no shape
    m.name = QFileInfo(path).fileName();
    m.sourceImagePath = storedPath;
    m.sourceImageOffset = QPointF(0.0, 0.0);
    m.sourceImageScale = QPointF(1.0, 1.0);
    m.sourceImageLockRatio = true;
    m_adj.masks.insert(0, m);
    m_activeMask = 0;
    m_maskMode = false;
    m_canvas->setMaskMode(MaskType::None, false);
    pushMaskGizmo();
    if (m_maskPreviewEnabled) retone();
    markEdited();
    emit masksChanged();
    kickoffImageLayerDecode(storedPath);
    return m_activeMask;
}

// Copies `sourcePath` into an app-managed file next to the base photo so the
// layer keeps working even if the original file is later moved or deleted.
// Falls back to referencing sourcePath directly if the copy fails.
QString RetouchTab::copyImageLayerAsset(const QString &sourcePath) {
    QString baseDir = m_path.isEmpty() ? QFileInfo(sourcePath).absolutePath()
                                        : QFileInfo(m_path).absolutePath();
    QString baseName = m_path.isEmpty() ? QStringLiteral("layer")
                                         : QFileInfo(m_path).completeBaseName();
    QString ext = QFileInfo(sourcePath).suffix();
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    QString destPath = QDir(baseDir).filePath(
        ext.isEmpty() ? QStringLiteral("%1.layer.%2").arg(baseName, uuid)
                       : QStringLiteral("%1.layer.%2.%3").arg(baseName, uuid, ext));

    if (QFile::copy(sourcePath, destPath)) return destPath;

    qWarning() << "copyImageLayerAsset: failed to copy" << sourcePath << "to" << destPath
               << "- referencing original file location instead";
    return sourcePath;
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

void RetouchTab::setActiveMaskText(const QString &text, const QString &family,
                                   double pixelSize, bool bold, bool italic) {
    if (m_activeMask < 0 || m_activeMask >= m_adj.masks.size()) return;
    Mask &m = m_adj.masks[m_activeMask];
    if (m.type != MaskType::Text) return;
    m.text = text;
    m.textFamily = family;
    m.textPixelSize = pixelSize;
    m.textBold = bold;
    m.textItalic = italic;
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
    Mask &m = m_adj.masks[m_activeMask];
    // Bake in the brush size/hardness/(paint) color at paint time so later
    // changes only affect new dabs, not ones already committed to the stroke.
    m.stroke.append(BrushStrokePoint{ptNorm, erase, m.brushRadius, m.hardness,
                                     m.paintColor.rgb()});
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
    // m_geomImg is the full-res oriented + healed + cropped base; apply tone,
    // then composite text last so it stays unaffected by tone/colour.
    QImage out = applyAdjustments(m_geomImg, toneOnly(m_adj));
    if (!m_adj.texts.isEmpty()) applyTexts(out, m_adj.texts, m_geomCropOffset);
    if (!m_adj.shapes.isEmpty()) applyShapes(out, m_adj.shapes, m_geomCropOffset);
    return out;
}
