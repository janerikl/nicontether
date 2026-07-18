#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>

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
    void render(const QImage &src, const Adjustments &adj);
signals:
    void done(const QImage &result);
};

// One open photo in the retouch window. Decodes its RAW asynchronously, then
// shows an interactive preview: geometry (rotate/flip/crop) is cached at full
// res and downscaled once; tone/colour sliders re-render only the small copy,
// so dragging stays smooth. Full-res render is produced on demand for export.
class RetouchTab : public QWidget {
    Q_OBJECT
public:
    explicit RetouchTab(const QString &path, QWidget *parent = nullptr);
    ~RetouchTab() override;

    QString path() const { return m_path; }
    Adjustments adjustments() const { return m_adj; }
    bool isReady() const { return !m_base.isNull(); }

    void setAdjustments(const Adjustments &a); // from the dock
    void setCropMode(bool on);
    void setCropAspect(double widthOverHeight);
    void applyCrop();
    void resetCrop();

    void setWbPickMode(bool on);
    void setHealMode(bool on);
    void setHealBrush(int radiusDisplayPx);
    void clearHeals();
    void showOriginal(bool on); // press-and-hold before/after
    void zoomFit();
    void setZoomPercent(double percent);
    double zoomPercent() const;

    bool isDirty() const { return m_dirty; }
    bool hasEdits() const;
    void saveEdits(); // write the sidecar and mark clean

    QImage renderFullRes() const; // for export

signals:
    void decoded(bool ok);
    void cropPending(bool hasSelection);
    void cropModeExited(); // crop applied internally (e.g. via Enter)
    void wbPicked();       // white balance was set from the eyedropper
    // Emitted when the save/edit state changes (for the thumbnail badge).
    void editStateChanged(bool dirty, bool hasEdits);
    void zoomChanged(double percent);

private slots:
    void onDecodeFinished();
    void onCanvasCrop(const QRect &r);
    void onColorPicked(const QColor &c);
    void onHealAt(const QPoint &imgPoint);
    void onRenderDone(const QImage &result);

private:
    void rebuildGeom();  // recompute oriented(+crop) full image + display base
    void retone();       // fast preview (defers clarity/sharpen while dragging)
    void retoneFull();   // full preview incl. clarity/sharpen (after idle)
    void requestRender(const QImage &src, const Adjustments &adj); // coalesced, async
    void markEdited(); // set dirty + emit editStateChanged

    QString m_path;
    QImage m_base;   // full-res decoded RAW (immutable)
    Adjustments m_adj;

    bool m_cropMode = false;
    bool m_dirty = false; // unsaved changes since last save/load
    int m_healRadiusDisplay = 20; // brush radius in display pixels
    QRect m_pendingCrop; // in oriented-image coords, awaiting Apply

    QImage m_geomImg;            // oriented (+crop unless cropMode), full res, untoned
    QImage m_scaled;             // display base, untoned
    double m_scaleFromGeom = 1.0;

    ImageCanvas *m_canvas = nullptr;
    QFutureWatcher<QImage> *m_watcher = nullptr;
    QTimer *m_fullRenderTimer = nullptr; // fires the full render after dragging stops

    QThread *m_renderThread = nullptr;
    RenderWorker *m_renderWorker = nullptr;
    bool m_rendering = false;   // a render is in flight
    bool m_hasPending = false;  // a newer request arrived while rendering
    QImage m_pendingSrc;
    Adjustments m_pendingAdj;

    QImage m_lastEdited;          // most recent edited render (for before/after)
    bool m_showingOriginal = false;
};
