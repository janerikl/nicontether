#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QElapsedTimer>

#include "edit/Adjustments.h"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QContextMenuEvent;

// Displays an image with zoom + pan, and supports crop rubber-band selection and
// a white-balance eyedropper. Zoom: Ctrl+wheel (anchored to the cursor),
// left-drag a marquee box to zoom to a region, Space+drag to pan. All crop/pick
// mapping goes through targetRect(), so it works at any zoom.
class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // An existing spot-heal op, in the same pixel space as the QImage passed
    // to setImage() (i.e. display-scaled, already oriented/cropped).
    struct HealMarker {
        QPointF pos;
        double radius = 0.0;
    };

    void setImage(const QImage &img); // already-adjusted image to show
    void setPlaceholder(const QString &text);
    void setCropMode(bool on);
    void setCropAspect(double widthOverHeight); // 0 = freeform
    void setPickMode(bool on); // white-balance eyedropper
    // Targeted color-range tool (Levels panel): click samples a target color,
    // horizontal drag adjusts it. A swatch of the picked color plus an amount
    // bar is drawn next to the click point while dragging.
    void setColorRangePickMode(bool on);
    void setColorRangeAmount(int amount); // -100..100, shown in the drag swatch
    void setHealMode(bool on); // spot-heal brush
    void setZoomMode(bool on); // zoom tool: enables marquee-drag zoom + Ctrl+wheel
    void setBrushRadius(int displayPx);

    // Local-mask editing. When a mask kind is set, dragging on the canvas
    // defines/updates the active mask's geometry (radial: centre→radius,
    // linear: p0→p1, brush: appends stroke points). setActiveMask supplies the
    // mask to draw as a gizmo (geometry is width-normalized, matching the
    // pipeline). Coordinates are emitted width-normalized.
    void setMaskMode(MaskType kind, bool on);
    void setActiveMask(bool has, const Mask &m);
    // Live coverage preview shown while painting a brush mask, independent of
    // whether any local adjustment sliders have been touched yet.
    // Existing heal spots, shown as a reddish overlay while hovering in heal
    // mode (Lightroom-style "visualize spots"); hidden once the mouse leaves.
    void setHealSpots(const QVector<HealMarker> &spots);
    void clearSelection();

    // Zoom control.
    void zoomFit();
    void setZoomPercent(double percent); // anchored to the view centre
    double zoomPercent() const { return m_scale * 100.0; }

    // Canvas background (right-click the canvas background to change it).
    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }

signals:
    void backgroundColorChanged(const QColor &color);
    void cropSelected(const QRect &imageRect);
    void commitCropRequested();
    void colorPicked(const QColor &color);

    // Targeted color-range gesture: press samples the pixel, moves report the
    // horizontal delta from the press point, release commits the gesture.
    void colorRangePickStarted(const QColor &color);
    void colorRangeDragged(int dxPixels);
    void colorRangeReleased();
    void healAt(const QPoint &imagePoint);
    void zoomChanged(double percent);
    void healBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while healing
    void maskBrushRadiusChanged(double radiusNorm); // ctrl+wheel resize while brush-masking
    void imageLayerTransformChanged(const QPointF &offsetNorm, const QPointF &scaleNorm,
                                    bool lockRatio);

    // Mask geometry edits (all points width-normalized).
    void maskRadialDragged(const QPointF &centerNorm, double radiusNorm);
    void maskLinearDragged(const QPointF &p0Norm, const QPointF &p1Norm);
    void maskBrushPoint(const QPointF &ptNorm, bool erase); // one stroke sample
    void maskEditFinished();                    // drag released → commit history
    void imageLayerDropped(const QString &path); // a photo was dropped in as a layer

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    enum class Drag { None, Creating, Moving, Resizing };
    enum class Handle { None, TopLeft, Top, TopRight, Right,
                        BottomRight, Bottom, BottomLeft, Left };

    QRect targetRect() const;          // where the image is painted (zoom+pan)
    QRectF imageLayerFrameRect() const;
    Handle imageLayerHandleAt(const QPoint &pos) const;
    QRect selectionRect() const;       // current rubber band in widget coords
    QRect selectionInImage() const;    // current rubber band mapped to image coords
    QPoint constrainedCorner(const QPoint &pos) const; // apply aspect + bounds
    Handle handleAt(const QPoint &pos) const; // which crop handle is under pos

    void relayoutFit();  // recompute scale/offset to fit + centre
    void zoomTo(double newScale, const QPointF &anchorWidgetPos);
    void clampPan();

    QImage m_img;
    QString m_placeholder = "Decoding…";
    bool m_cropMode = false;
    bool m_pickMode = false;

    // Targeted color-range tool state.
    bool m_colorRangeMode = false;
    bool m_colorRangeDragging = false;
    QPoint m_colorRangeStart;   // widget coords of the press
    QColor m_colorRangeColor;   // sampled target color (swatch fill)
    int m_colorRangeChannel = 0; // 0=R,1=G,2=B — dominant channel (swatch border)
    int m_colorRangeAmount = 0;  // -100..100, shown in the amount bar
    bool m_healMode = false;
    bool m_zoomMode = false; // gates marquee-drag zoom + Ctrl+wheel zoom
    int m_brushRadius = 20; // display px, for the brush cursor

    // Local-mask editing state.
    bool m_maskMode = false;
    MaskType m_maskKind = MaskType::Radial;
    bool m_hasActiveMask = false;
    Mask m_activeMask;
    bool m_hasActiveImageLayer = false;
    QImage m_maskOverlay; // cached brush-coverage preview for m_activeMask        // geometry to draw as a gizmo
    BrushRasterCache m_maskOverlayCache; // incremental rasterization cache for m_maskOverlay
    bool m_maskDragging = false;
    bool m_maskErasing = false; // Alt held while brush-masking: erase instead of paint
    QPointF m_maskCenterNorm; // radial centre / linear p0 captured at press
    QPointF m_lastBrushNorm{-1, -1};
    QPointF normPointAt(const QPoint &pos) const; // widget → width-normalized
    QPoint m_mousePos;
    QVector<HealMarker> m_healSpots;
    Drag m_drag = Drag::None;
    double m_cropAspect = 0.0; // width/height; 0 = freeform
    QPoint m_p0, m_p1; // crop selection corners (widget coords)
    QPoint m_moveStart;
    QRect m_rectAtMoveStart;
    Handle m_activeHandle = Handle::None;
    QRect m_rectAtDragStart; // selection rect (widget coords) captured at press

    bool m_imageDragging = false;
    Handle m_imageActiveHandle = Handle::None;
    QPoint m_imageMoveStart;
    QRectF m_imageFrameAtDragStart;
    QPointF m_imageOffsetAtDragStart;
    QPointF m_imageScaleAtDragStart;
    // Throttles imageLayerTransformChanged emissions during a drag so the
    // (expensive) model re-render isn't requested faster than ~60fps; the
    // frame/handles themselves still update every move via m_activeMask,
    // which is written locally and doesn't wait on that round trip.
    QElapsedTimer m_imageDragEmitThrottle;

    // Zoom / pan.
    double m_scale = 1.0;   // widget px per image px
    QPointF m_topLeft;      // widget coords of image (0,0)
    bool m_fit = true;      // auto-fit until the user zooms

    // Marquee zoom + pan drags (normal mode).
    bool m_marquee = false;
    QPoint m_mp0, m_mp1;
    bool m_panning = false;
    QPoint m_panLast;
    bool m_spaceDown = false;

    bool m_dragHighlight = false; // a valid image drag is hovering the canvas
    QColor m_backgroundColor = QColor(30, 30, 30);
};
