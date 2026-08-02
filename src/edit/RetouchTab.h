#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QColor>

#include "edit/Adjustments.h"

class ImageCanvas;
class QTimer;
class QThread;
template <typename T> class QFutureWatcher;

// Runs applyAdjustments off the GUI thread. Lives on its own QThread; one job
// at a time (the tab coalesces to the latest request).
class RenderWorker : public QObject {
    Q_OBJECT
public slots:
    // `maskSnapshotIndex >= 0` additionally requests the cumulative composite
    // through that mask index, returned as `maskSnapshot` in `done` (used to
    // feed the per-layer Levels histogram; see RetouchTab::maskPreviewImage).
    void render(const QImage &src, const Adjustments &adj, int maskSnapshotIndex);
signals:
    void done(const QImage &result, const QImage &maskSnapshot);

private:
    // Persists across calls (this worker's queued slot invocations run
    // serially on one thread) so brush/paint mask coverage only needs to be
    // rasterized incrementally as a stroke grows; see BrushRasterCache.
    QVector<BrushRasterCache> m_brushCache;
};

// One open photo in the retouch window. Decodes its RAW asynchronously, then
// shows an interactive preview: geometry (rotate/flip/crop) is cached at full
// res and downscaled once; tone/colour sliders re-render only the small copy,
// so dragging stays smooth. Full-res render is produced on demand for export.
class RetouchTab : public QWidget {
    Q_OBJECT
public:
    explicit RetouchTab(const QString &path, QWidget *parent = nullptr);
    explicit RetouchTab(const QSize &blankSize, QWidget *parent = nullptr); // File > New
    ~RetouchTab() override;

    QString path() const { return m_path; }
    void assignPath(const QString &path); // File > New's first save: adopt a real backing path
    Adjustments adjustments() const { return m_adj; }
    bool isReady() const { return !m_base.isNull(); }

    void setAdjustments(const Adjustments &a); // from the dock
    void setCropMode(bool on);
    void setCropAspect(double widthOverHeight);
    void applyCrop();
    void resetCrop();

    void setWbPickMode(bool on);
    void setColorRangePickMode(bool on); // Levels targeted color adjustment
    void setHealMode(bool on);
    void setZoomMode(bool on); // zoom tool: marquee-drag + Ctrl+wheel zoom
    void setHealBrush(int radiusDisplayPx);
    void clearHeals();
    void setEraseMode(bool on);
    void setEraseBrush(int radiusDisplayPx);

    // Text tool.
    void setTextMode(bool on);
    void deleteActiveText();
    int activeTextIndex() const { return m_activeText; }
    // Style of the active text, or the "next new text" defaults if none is
    // selected — what the options bar should display.
    TextOp activeTextStyle() const;
    // Each setter applies to the active text if one is selected, otherwise
    // updates the defaults used for the next newly-placed text.
    void setTextFont(const QString &family, double pixelSize, bool bold, bool italic);
    void setTextColor(const QColor &color);
    void setTextOutline(bool enabled, const QColor &color, double width);
    void setTextShadow(bool enabled, const QPointF &offset, double blur, double opacity,
                       const QColor &color);
    void setTextBackground(bool enabled, const QColor &color, double opacity, double padding);

    // Shape tool.
    void setShapeMode(bool on);
    void setActiveShapeType(ShapeType t);
    void deleteActiveShape();
    int activeShapeIndex() const { return m_activeShape; }
    // Style of the active shape, or the "next new shape" defaults if none is
    // selected — what the options bar should display.
    ShapeOp activeShapeStyle() const;
    // Each setter applies to the active shape if one is selected, otherwise
    // updates the defaults used for the next newly-created shape.
    void setShapeSides(int sides);
    void setShapeInnerRadiusRatio(double ratio);
    void setShapeFill(bool enabled, const QColor &color);
    void setShapeStroke(bool enabled, const QColor &color, double width);

    // Local adjustment masks.
    void setMaskMode(bool on);              // enter/leave mask editing on the canvas
    int addMask(MaskType type);             // append + select; returns its index
    int addImageLayer(const QString &path); // append an image layer; returns its index
    int duplicateActiveMask();              // copy + insert above; returns its index
    void selectMask(int index);             // -1 = none
    void deleteActiveMask();
    void setActiveMaskType(MaskType type);  // add/remove/change the layer's mask
    void setActiveMaskAdjust(const MaskAdjust &a);
    void setActiveMaskImageTransform(double offsetX, double offsetY, double scaleX,
                                     double scaleY, bool lockRatio);
    void setActiveMaskShape(bool inverted, double feather, double hardness,
                            double brushRadius, bool autoMask);
    void setPaintColor(const QColor &color); // no-op unless the active layer is MaskType::Paint
    void setActiveMaskText(const QString &text, const QString &family, double pixelSize,
                           bool bold, bool italic); // no-op unless the active layer is MaskType::Text
    void setActiveMaskOpacity(double opacity);       // 0..1
    void setActiveMaskBlend(BlendMode mode);
    void setActiveMaskVisible(bool visible);
    void setMaskVisible(int index, bool visible); // toggle any layer, not just the active one
    void setActiveMaskName(const QString &name);
    void moveMask(int from, int to);                 // reorder within the stack
    const QVector<Mask> &masks() const { return m_adj.masks; }
    int activeMaskIndex() const { return m_activeMask; }
    void showOriginal(bool on); // press-and-hold before/after
    void zoomFit();
    void setZoomPercent(double percent);
    double zoomPercent() const;

    bool isDirty() const { return m_dirty; }
    bool hasEdits() const;
    void saveEdits(); // write the sidecar and mark clean

    void undo();
    void redo();
    bool canUndo() const { return m_histIndex > 0; }
    bool canRedo() const { return m_histIndex >= 0 && m_histIndex < m_history.size() - 1; }

    const QVector<Adjustments> &history() const { return m_history; }
    int historyIndex() const { return m_histIndex; }
    void jumpToHistory(int index); // set position to index and apply it

    QImage renderFullRes() const; // for export

    // Latest toned preview render (display-scaled). Empty until first render.
    QImage previewImage() const { return m_lastEdited; }

    // Per-layer Levels histogram feed: while enabled (Layers dock visible),
    // the render pipeline additionally produces the cumulative composite
    // through the active mask, pushed to whoever wants to draw its histogram.
    void setMaskPreviewEnabled(bool on);
    QImage maskPreviewImage() const { return m_maskPreviewImage; }

signals:
    void decoded(bool ok);
    void cropPending(bool hasSelection);
    void cropModeExited(); // crop applied internally (e.g. via Enter)
    void wbPicked();       // white balance was set from the eyedropper
    // Emitted when the save/edit state changes (for the thumbnail badge).
    void editStateChanged(bool dirty, bool hasEdits);
    void zoomChanged(double percent);
    void historyChanged(bool canUndo, bool canRedo);
    void historyListChanged(); // history entries or current index changed
    void adjustmentsReplaced(); // undo/redo swapped the whole adjustment set
    void healBrushChanged(int radiusDisplayPx); // ctrl+wheel resized the brush
    void eraseBrushChanged(int radiusDisplayPx); // ctrl+wheel resized the erase brush
    void previewUpdated(); // a new toned preview render is available
    void maskPreviewUpdated(); // a new per-layer histogram source image is available
    void masksChanged();   // mask list or active-mask geometry changed
    void maskBrushChanged(double radiusNorm); // ctrl+wheel resized the mask brush
    void textsChanged();   // text list, active text, or its style changed
    void shapesChanged();  // shape list, active shape, or its style changed

private slots:
    void onDecodeFinished();
    void onCanvasCrop(const QRect &r);
    void onColorPicked(const QColor &c);
    void onColorRangePickStarted(const QColor &c);
    void onColorRangeDragged(int dxPixels);
    void onColorRangeReleased();
    void onHealAt(const QPoint &imgPoint);
    void onTextPlaceRequested(const QPoint &imgPoint);
    void onTextSelected(int index);
    void onTextDeselected();
    void onTextMoved(int index, const QPointF &newImgPos);
    void onTextRotated(int index, double newRotationDegrees);
    void onTextEditRequested(int index);
    void onTextEditCommitted(int index, const QString &text);
    void onTextEditCancelled(int index);
    void onTextLiveContentChanged(int index, const QString &text);
    void onTextDeleteRequested(int index);
    void onTextResizeStarted(int index);
    void onTextResized(int index, double ratio);
    void onShapeCreateRequested(ShapeType type, const QRectF &imageRect);
    void onShapeSelected(int index);
    void onShapeDeselected();
    void onShapeMoved(int index, const QPointF &deltaImage);
    void onShapeResized(int index, const QRectF &newImageRect);
    void onShapeLineEndpointsChanged(int index, const QPointF &p1, const QPointF &p2);
    void onShapeRotated(int index, double newRotationDegrees);
    void onShapeDeleteRequested(int index);
    void onShapeDuplicateRequested(int index);
    void onShapeRaiseRequested(int index);
    void onShapeLowerRequested(int index);
    void onEraseAt(const QPointF &ptNorm);
    void onEraseFinished();
    void onRenderDone(const QImage &result, const QImage &maskSnapshot);
    void onMaskRadial(const QPointF &centerNorm, double radiusNorm);
    void onMaskLinear(const QPointF &p0Norm, const QPointF &p1Norm);
    void onMaskBrushPoint(const QPointF &ptNorm, bool erase);
    void onMaskEditFinished();

private:
    void rebuildGeom();  // recompute oriented(+crop) full image + display base
    void retone();       // fast preview (defers clarity/sharpen while dragging)
    void retoneFull();   // full preview incl. clarity/sharpen (after idle)
    void requestRender(const QImage &src, const Adjustments &adj, int maskSnapshotIndex = -1); // coalesced, async
    int maskPreviewIndex() const { return m_maskPreviewEnabled ? m_activeMask : -1; }
    void markEdited(); // set dirty + emit editStateChanged
    void commitHistory();     // snapshot current adjustments (coalesced)
    void applyHistoryState(); // apply m_history[m_histIndex]
    void updateHealSpots();   // push heal-op markers (display coords) to the canvas
    void updateTextMarkers(); // push text-op markers (display coords) to the canvas
    void updateShapeMarkers(); // push shape-op markers (display coords) to the canvas
    void pushMaskGizmo();     // sync active mask geometry to the canvas
    void kickoffImageLayerDecode(const QString &path); // async-decode an image layer's source
    QString copyImageLayerAsset(const QString &sourcePath); // copy a layer source next to m_path so it survives move/delete
    void setupCanvasAndWiring(); // shared canvas creation + connect()s for both constructors

    QString m_path;
    QImage m_base;   // full-res decoded RAW (immutable)
    Adjustments m_adj;

    bool m_cropMode = false;
    bool m_maskMode = false;
    int m_activeMask = -1; // index into m_adj.masks, or -1
    bool m_textMode = false;
    int m_activeText = -1;   // index into m_adj.texts, or -1
    int m_newTextIndex = -1; // index of a just-placed, not-yet-committed draft, or -1
    int m_textEditIndex = -1; // index currently open in the inline editor, or -1
    TextOp m_textDefaults;   // style applied to the next newly-placed text
    double m_textResizeStartPixelSize = 48.0; // captured at corner-drag start

    bool m_shapeMode = false;
    int m_activeShape = -1;   // index into m_adj.shapes, or -1
    ShapeOp m_shapeDefaults;  // style/type applied to the next newly-created shape
    QRectF m_shapeMoveStartRect;   // active shape's rect at move-drag start
    QPointF m_shapeMoveStartP1, m_shapeMoveStartP2; // active Line's endpoints at move-drag start
    bool m_dirty = false; // unsaved changes since last save/load
    int m_healRadiusDisplay = 20; // brush radius in display pixels
    int m_eraseRadiusDisplay = 20; // erase brush radius in display pixels
    int m_crIndex = -1;      // colorRanges entry being dragged, or -1
    int m_crBaseAmount = 0;  // that entry's amount at drag start
    QRect m_pendingCrop; // in oriented-image coords, awaiting Apply

    QImage m_geomImg;            // oriented (+crop unless cropMode), full res, untoned
    QPoint m_geomCropOffset;     // crop top-left baked into m_geomImg/m_scaled, or (0,0)
    QImage m_scaled;             // display base, untoned
    double m_scaleFromGeom = 1.0;

    ImageCanvas *m_canvas = nullptr;
    QFutureWatcher<QImage> *m_watcher = nullptr;
    QTimer *m_fullRenderTimer = nullptr; // fires the full render after dragging stops
    QTimer *m_commitTimer = nullptr;     // coalesces edits into one undo step
    QVector<Adjustments> m_history;      // committed adjustment snapshots
    int m_histIndex = -1;

    QThread *m_renderThread = nullptr;
    RenderWorker *m_renderWorker = nullptr;
    bool m_rendering = false;   // a render is in flight
    bool m_hasPending = false;  // a newer request arrived while rendering
    QImage m_pendingSrc;
    Adjustments m_pendingAdj;
    int m_pendingMaskIdx = -1;

    QImage m_lastEdited;          // most recent edited render (for before/after)
    bool m_showingOriginal = false;

    bool m_maskPreviewEnabled = false; // Layers dock visible -> compute per-layer histogram source
    QImage m_maskPreviewImage;
};
