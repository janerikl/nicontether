#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QPoint>
#include <QPointF>

// Displays an image with zoom + pan, and supports crop rubber-band selection and
// a white-balance eyedropper. Zoom: Ctrl+wheel (anchored to the cursor),
// left-drag a marquee box to zoom to a region, Space+drag to pan. All crop/pick
// mapping goes through targetRect(), so it works at any zoom.
class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    void setImage(const QImage &img); // already-adjusted image to show
    void setPlaceholder(const QString &text);
    void setCropMode(bool on);
    void setCropAspect(double widthOverHeight); // 0 = freeform
    void setPickMode(bool on); // white-balance eyedropper
    void setHealMode(bool on); // spot-heal brush
    void setBrushRadius(int displayPx);
    void clearSelection();

    // Zoom control.
    void zoomFit();
    void setZoomPercent(double percent); // anchored to the view centre
    double zoomPercent() const { return m_scale * 100.0; }

signals:
    void cropSelected(const QRect &imageRect);
    void commitCropRequested();
    void colorPicked(const QColor &color);
    void healAt(const QPoint &imagePoint);
    void zoomChanged(double percent);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QRect targetRect() const;          // where the image is painted (zoom+pan)
    QRect selectionRect() const;       // current rubber band in widget coords
    QRect selectionInImage() const;    // current rubber band mapped to image coords
    QPoint constrainedCorner(const QPoint &pos) const; // apply aspect + bounds

    void relayoutFit();  // recompute scale/offset to fit + centre
    void zoomTo(double newScale, const QPointF &anchorWidgetPos);
    void clampPan();

    enum class Drag { None, Creating, Moving };

    QImage m_img;
    QString m_placeholder = "Decoding…";
    bool m_cropMode = false;
    bool m_pickMode = false;
    bool m_healMode = false;
    int m_brushRadius = 20; // display px, for the brush cursor
    QPoint m_mousePos;
    Drag m_drag = Drag::None;
    double m_cropAspect = 0.0; // width/height; 0 = freeform
    QPoint m_p0, m_p1; // crop selection corners (widget coords)
    QPoint m_moveStart;
    QRect m_rectAtMoveStart;

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
};
