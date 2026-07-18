#include "edit/ImageCanvas.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <cmath>
#include <algorithm>

namespace {
constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 8.0;
}

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(480, 320);
    setAutoFillBackground(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setPalette(pal);
}

void ImageCanvas::setImage(const QImage &img) {
    bool sizeChanged = (img.size() != m_img.size());
    m_img = img;
    if (m_fit || sizeChanged) {
        m_fit = true;
        relayoutFit();
        emit zoomChanged(zoomPercent());
    }
    update();
}

void ImageCanvas::setPlaceholder(const QString &text) {
    m_img = QImage();
    m_placeholder = text;
    update();
}

void ImageCanvas::setCropMode(bool on) {
    m_cropMode = on;
    if (!on) m_drag = Drag::None;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setPickMode(bool on) {
    m_pickMode = on;
    setCursor(on ? Qt::CrossCursor : (m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor));
}

void ImageCanvas::setCropAspect(double widthOverHeight) {
    m_cropAspect = widthOverHeight > 0 ? widthOverHeight : 0.0;
    if (m_cropAspect > 0 && !QRect(m_p0, m_p1).normalized().isEmpty()) {
        m_p1 = constrainedCorner(m_p1);
        update();
        emit cropSelected(selectionInImage());
    }
}

void ImageCanvas::clearSelection() {
    m_drag = Drag::None;
    m_p0 = m_p1 = QPoint();
    update();
}

// ---- Zoom / pan ------------------------------------------------------------

void ImageCanvas::relayoutFit() {
    if (m_img.isNull()) return;
    double fs = std::min(double(width()) / m_img.width(),
                         double(height()) / m_img.height());
    m_scale = fs;
    m_topLeft = QPointF((width() - m_img.width() * fs) / 2.0,
                        (height() - m_img.height() * fs) / 2.0);
}

void ImageCanvas::clampPan() {
    if (m_img.isNull()) return;
    double iw = m_img.width() * m_scale, ih = m_img.height() * m_scale;
    if (iw <= width()) m_topLeft.setX((width() - iw) / 2.0);
    else m_topLeft.setX(std::clamp(m_topLeft.x(), double(width()) - iw, 0.0));
    if (ih <= height()) m_topLeft.setY((height() - ih) / 2.0);
    else m_topLeft.setY(std::clamp(m_topLeft.y(), double(height()) - ih, 0.0));
}

void ImageCanvas::zoomTo(double newScale, const QPointF &anchor) {
    if (m_img.isNull()) return;
    newScale = std::clamp(newScale, kMinScale, kMaxScale);
    QPointF imgPt = (anchor - m_topLeft) / m_scale; // image-space point under anchor
    m_scale = newScale;
    m_fit = false;
    m_topLeft = anchor - imgPt * newScale;
    clampPan();
    update();
    emit zoomChanged(zoomPercent());
}

void ImageCanvas::zoomFit() {
    m_fit = true;
    relayoutFit();
    update();
    emit zoomChanged(zoomPercent());
}

void ImageCanvas::setZoomPercent(double percent) {
    zoomTo(percent / 100.0, QPointF(width() / 2.0, height() / 2.0));
}

void ImageCanvas::resizeEvent(QResizeEvent *) {
    if (m_fit) relayoutFit();
    else clampPan();
}

QRect ImageCanvas::targetRect() const {
    if (m_img.isNull()) return QRect();
    return QRectF(m_topLeft, QSizeF(m_img.width() * m_scale,
                                    m_img.height() * m_scale)).toRect();
}

// ---- Crop mapping ----------------------------------------------------------

QRect ImageCanvas::selectionRect() const {
    return QRect(m_p0, m_p1).normalized();
}

QPoint ImageCanvas::constrainedCorner(const QPoint &pos) const {
    if (m_cropAspect <= 0) return pos;
    QRect tr = targetRect();
    int sx = pos.x() >= m_p0.x() ? 1 : -1;
    int sy = pos.y() >= m_p0.y() ? 1 : -1;
    double w = std::abs(pos.x() - m_p0.x());
    double h = std::abs(pos.y() - m_p0.y());
    if (w / m_cropAspect >= h) h = w / m_cropAspect;
    else w = h * m_cropAspect;
    double maxW = sx > 0 ? tr.right() - m_p0.x() : m_p0.x() - tr.left();
    double maxH = sy > 0 ? tr.bottom() - m_p0.y() : m_p0.y() - tr.top();
    if (maxW < 0) maxW = 0;
    if (maxH < 0) maxH = 0;
    if (w > maxW) { w = maxW; h = w / m_cropAspect; }
    if (h > maxH) { h = maxH; w = h * m_cropAspect; }
    return m_p0 + QPoint(int(sx * w), int(sy * h));
}

QRect ImageCanvas::selectionInImage() const {
    QRect tr = targetRect();
    if (m_img.isNull() || tr.isEmpty()) return QRect();
    QRect sel = selectionRect().intersected(tr);
    if (sel.isEmpty()) return QRect();
    double sx = double(m_img.width()) / tr.width();
    double sy = double(m_img.height()) / tr.height();
    int ix = int((sel.x() - tr.x()) * sx);
    int iy = int((sel.y() - tr.y()) * sy);
    int iw = int(sel.width() * sx);
    int ih = int(sel.height() * sy);
    return QRect(ix, iy, iw, ih).intersected(m_img.rect());
}

// ---- Paint -----------------------------------------------------------------

void ImageCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    if (m_img.isNull()) {
        p.setPen(Qt::lightGray);
        p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }
    QRect tr = targetRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1.0);
    p.drawImage(tr, m_img);

    if (m_cropMode && (m_drag != Drag::None || !selectionRect().isEmpty())) {
        QRect sel = selectionRect().intersected(tr);
        if (!sel.isEmpty()) {
            QRegion outside(tr);
            outside = outside.subtracted(QRegion(sel));
            p.setClipRegion(outside);
            p.fillRect(tr, QColor(0, 0, 0, 120));
            p.setClipping(false);
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawRect(sel);
        }
    }

    if (m_marquee) {
        QRect box = QRect(m_mp0, m_mp1).normalized();
        p.setPen(QPen(QColor(120, 180, 255), 1, Qt::DashLine));
        p.setBrush(QColor(120, 180, 255, 40));
        p.drawRect(box);
    }
}

// ---- Mouse -----------------------------------------------------------------

void ImageCanvas::mousePressEvent(QMouseEvent *ev) {
    if (m_img.isNull()) return;

    // White-balance eyedropper.
    if (m_pickMode && ev->button() == Qt::LeftButton) {
        QRect tr = targetRect();
        if (tr.contains(ev->pos())) {
            double sx = double(m_img.width()) / tr.width();
            double sy = double(m_img.height()) / tr.height();
            int ix = std::clamp(int((ev->pos().x() - tr.x()) * sx), 0, m_img.width() - 1);
            int iy = std::clamp(int((ev->pos().y() - tr.y()) * sy), 0, m_img.height() - 1);
            emit colorPicked(m_img.pixelColor(ix, iy));
        }
        return;
    }

    // Crop mode.
    if (m_cropMode && ev->button() == Qt::LeftButton) {
        if (selectionRect().contains(ev->pos())) {
            m_drag = Drag::Moving;
            m_moveStart = ev->pos();
            m_rectAtMoveStart = selectionRect();
            setCursor(Qt::ClosedHandCursor);
        } else {
            m_drag = Drag::Creating;
            m_p0 = m_p1 = ev->pos();
        }
        update();
        return;
    }

    // Normal mode: Space+drag pans, plain drag draws a zoom marquee.
    if (ev->button() == Qt::LeftButton) {
        if (m_spaceDown) {
            m_panning = true;
            m_panLast = ev->pos();
            setCursor(Qt::ClosedHandCursor);
        } else {
            m_marquee = true;
            m_mp0 = m_mp1 = ev->pos();
        }
        update();
    } else if (ev->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panLast = ev->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *ev) {
    if (m_drag == Drag::Creating) {
        m_p1 = m_cropAspect > 0 ? constrainedCorner(ev->pos()) : ev->pos();
        update();
    } else if (m_drag == Drag::Moving) {
        QRect tr = targetRect();
        QRect r = m_rectAtMoveStart.translated(ev->pos() - m_moveStart);
        if (r.left() < tr.left()) r.moveLeft(tr.left());
        if (r.top() < tr.top()) r.moveTop(tr.top());
        if (r.right() > tr.right()) r.moveRight(tr.right());
        if (r.bottom() > tr.bottom()) r.moveBottom(tr.bottom());
        m_p0 = r.topLeft();
        m_p1 = r.bottomRight();
        update();
    } else if (m_panning) {
        m_topLeft += QPointF(ev->pos() - m_panLast);
        m_panLast = ev->pos();
        clampPan();
        update();
    } else if (m_marquee) {
        m_mp1 = ev->pos();
        update();
    } else if (m_cropMode) {
        setCursor(selectionRect().contains(ev->pos()) ? Qt::SizeAllCursor
                                                       : Qt::CrossCursor);
    }
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *ev) {
    if (m_drag != Drag::None && ev->button() == Qt::LeftButton) {
        m_drag = Drag::None;
        setCursor(m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor);
        update();
        emit cropSelected(selectionInImage());
        return;
    }
    if (m_panning && (ev->button() == Qt::LeftButton || ev->button() == Qt::MiddleButton)) {
        m_panning = false;
        setCursor(m_spaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    if (m_marquee && ev->button() == Qt::LeftButton) {
        m_marquee = false;
        QRect box = QRect(m_mp0, m_mp1).normalized();
        update();
        if (box.width() < 8 || box.height() < 8) return; // treat as a click
        // Zoom so the boxed region fills the view.
        double imgW = box.width() / m_scale, imgH = box.height() / m_scale;
        double cx = (box.center().x() - m_topLeft.x()) / m_scale;
        double cy = (box.center().y() - m_topLeft.y()) / m_scale;
        double newScale = std::clamp(std::min(width() / imgW, height() / imgH),
                                     kMinScale, kMaxScale);
        m_scale = newScale;
        m_fit = false;
        m_topLeft = QPointF(width() / 2.0 - cx * newScale,
                            height() / 2.0 - cy * newScale);
        clampPan();
        update();
        emit zoomChanged(zoomPercent());
    }
}

void ImageCanvas::wheelEvent(QWheelEvent *ev) {
    if ((ev->modifiers() & Qt::ControlModifier) && !m_img.isNull()) {
        double f = ev->angleDelta().y() > 0 ? 1.25 : 0.8;
        zoomTo(m_scale * f, ev->position());
        ev->accept();
        return;
    }
    QWidget::wheelEvent(ev);
}

void ImageCanvas::keyPressEvent(QKeyEvent *ev) {
    if (m_cropMode && (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) &&
        !selectionInImage().isEmpty()) {
        emit commitCropRequested();
        ev->accept();
        return;
    }
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat() && !m_cropMode) {
        m_spaceDown = true;
        if (!m_panning) setCursor(Qt::OpenHandCursor);
        ev->accept();
        return;
    }
    QWidget::keyPressEvent(ev);
}

void ImageCanvas::keyReleaseEvent(QKeyEvent *ev) {
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat()) {
        m_spaceDown = false;
        if (!m_panning) setCursor(m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor);
        ev->accept();
        return;
    }
    QWidget::keyReleaseEvent(ev);
}
