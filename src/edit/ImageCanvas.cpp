#include "edit/ImageCanvas.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPixmap>
#include <QPainterPath>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QContextMenuEvent>
#include <QMenu>
#include <QColorDialog>
#include <cmath>
#include <algorithm>

namespace {
bool hasLocalFileUrl(const QMimeData *data) {
    if (!data || !data->hasUrls()) return false;
    for (const QUrl &u : data->urls())
        if (u.isLocalFile()) return true;
    return false;
}
} // namespace

namespace {
constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 8.0;
constexpr int kHealBrushMin = 4;
constexpr int kHealBrushMax = 80;
constexpr double kMaskBrushMin = 0.01;
constexpr double kMaskBrushMax = 0.40;
constexpr double kMaskBrushStep = 0.01;
constexpr double kImageLayerScaleMin = 0.10;
constexpr double kImageLayerScaleMax = 3.00;

// A magnifying-glass cursor, drawn once and cached. Hotspot sits at the centre
// of the lens so it lines up with the point being zoomed.
const QCursor &zoomCursor() {
    static const QCursor c = [] {
        constexpr int px = 28;
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(Qt::white, 2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(4, 4, 14, 14));   // lens, centred at (11, 11)
        p.drawLine(QPointF(15, 15), QPointF(23, 23)); // handle
        p.end();
        return QCursor(pm, 11, 11);
    }();
    return c;
}

// An eyedropper/pipette cursor for the white-balance pick tool, styled after
// Photoshop's: a black dropper silhouette with a white outline so it reads
// against any background, tip at bottom-left where the sample is taken.
const QCursor &pipetteCursor() {
    static const QCursor c = [] {
        constexpr int px = 32;
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Build the glyph in local space (tip at origin, barrel running to
        // the right), then rotate the whole outline -45deg and place it so
        // the tip lands at the bottom-left, bulb at top-right.
        QPainterPath nib;
        nib.moveTo(-9, 0);
        nib.lineTo(-4, -2.2);
        nib.lineTo(-4, 2.2);
        nib.closeSubpath();

        QPainterPath body;
        body.addRoundedRect(QRectF(-4, -2.5, 24, 5), 2.2, 2.2);

        QPainterPath bulb;
        bulb.addEllipse(QRectF(17, -4, 8, 8));

        QPainterPath outline = nib.united(body).united(bulb);

        QTransform t;
        t.translate(6, 25);
        t.rotate(-45);
        outline = t.map(outline);

        p.setPen(QPen(Qt::white, 2.4));
        p.setBrush(Qt::black);
        p.drawPath(outline);

        // Diagonal band near the tip, a common pipette-glyph detail.
        QLineF band(-1, -2.2, -1, 2.2);
        band = t.map(band);
        p.setPen(QPen(Qt::white, 1.4));
        p.drawLine(band);

        p.end();
        return QCursor(pm, 4, 30);
    }();
    return c;
}
}

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(480, 320);
    setAutoFillBackground(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setPalette(pal);
}

void ImageCanvas::setImage(const QImage &img) {
    bool sizeChanged = (img.size() != m_img.size());
    // The editing pipeline works in 16-bit; the screen is 8-bit, so dither
    // down here rather than letting Qt's paint engine silently truncate
    // (which would reintroduce banding on-screen).
    m_img = ditherTo8Bit(img);
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
    if (on) setCursor(pipetteCursor());
    else setCursor(m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor);
}

void ImageCanvas::setColorRangePickMode(bool on) {
    m_colorRangeMode = on;
    if (!on) m_colorRangeDragging = false;
    setCursor(on ? pipetteCursor() : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setColorRangeAmount(int amount) {
    if (m_colorRangeAmount == amount) return;
    m_colorRangeAmount = amount;
    if (m_colorRangeDragging) update();
}

void ImageCanvas::setHealMode(bool on) {
    m_healMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setEraseMode(bool on) {
    m_eraseMode = on;
    if (!on) m_eraseDragging = false;
    update();
}

void ImageCanvas::setZoomMode(bool on) {
    m_zoomMode = on;
    if (on) setCursor(zoomCursor());
    else setCursor(Qt::ArrowCursor);
}

void ImageCanvas::setMaskMode(MaskType kind, bool on) {
    m_maskMode = on;
    m_maskKind = kind;
    if (!on) m_maskDragging = false;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setActiveMask(bool has, const Mask &m) {
    m_hasActiveMask = has;
    m_activeMask = m;
    m_hasActiveImageLayer = has && m.isImageLayer();
    if (!m_hasActiveImageLayer) {
        m_imageDragging = false;
        m_imageActiveHandle = Handle::None;
    }
    if (has) m_maskKind = m.type;
    // Recompute the live brush-coverage preview so the painted area is visible
    // immediately, even before any adjustment slider has been touched.
    if (has && m.type == MaskType::Brush && !m.stroke.isEmpty() && !m_img.isNull())
        m_maskOverlay = maskCoverageOverlay(m, m_img.width(), m_img.height(),
                                            QColor(120, 200, 255), 140, m_img,
                                            &m_maskOverlayCache);
    else
        m_maskOverlay = QImage();
    update();
}

QPointF ImageCanvas::normPointAt(const QPoint &pos) const {
    QRect tr = targetRect();
    if (m_img.isNull() || tr.width() <= 0 || tr.height() <= 0)
        return QPointF(0, 0);
    double W = m_img.width();
    double ix = (pos.x() - tr.x()) * (m_img.width() / double(tr.width()));
    double iy = (pos.y() - tr.y()) * (m_img.height() / double(tr.height()));
    return QPointF(ix / W, iy / W); // both axes normalized to width
}

void ImageCanvas::setBrushRadius(int displayPx) {
    m_brushRadius = displayPx;
    if (m_healMode) update();
}

void ImageCanvas::setHealSpots(const QVector<HealMarker> &spots) {
    m_healSpots = spots;
    if (m_healMode) update();
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

QRectF ImageCanvas::imageLayerFrameRect() const {
    if (!m_hasActiveImageLayer || m_img.isNull()) return QRectF();
    const double w = m_img.width() * std::max(0.01, m_activeMask.sourceImageScale.x());
    const double h = m_img.height() * std::max(0.01, m_activeMask.sourceImageScale.y());
    const double cx = m_img.width() * (0.5 + 0.5 * std::clamp(m_activeMask.sourceImageOffset.x(), -1.0, 1.0));
    const double cy = m_img.height() * (0.5 + 0.5 * std::clamp(m_activeMask.sourceImageOffset.y(), -1.0, 1.0));
    return QRectF(cx - w / 2.0, cy - h / 2.0, w, h);
}

ImageCanvas::Handle ImageCanvas::imageLayerHandleAt(const QPoint &pos) const {
    QRectF r = imageLayerFrameRect();
    if (r.isEmpty()) return Handle::None;
    QPointF imgPos = (QPointF(pos) - m_topLeft) / m_scale;
    const double t = 10.0 / std::max(0.01, m_scale);
    auto near = [&](double a, double b) { return std::abs(a - b) <= t; };
    bool onLeft   = near(imgPos.x(), r.left());
    bool onRight  = near(imgPos.x(), r.right());
    bool onTop    = near(imgPos.y(), r.top());
    bool onBottom = near(imgPos.y(), r.bottom());
    bool inX = imgPos.x() >= r.left() - t && imgPos.x() <= r.right() + t;
    bool inY = imgPos.y() >= r.top() - t && imgPos.y() <= r.bottom() + t;
    if (onTop && onLeft)       return Handle::TopLeft;
    if (onTop && onRight)      return Handle::TopRight;
    if (onBottom && onLeft)    return Handle::BottomLeft;
    if (onBottom && onRight)   return Handle::BottomRight;
    if (onTop && inX)          return Handle::Top;
    if (onBottom && inX)       return Handle::Bottom;
    if (onLeft && inY)         return Handle::Left;
    if (onRight && inY)        return Handle::Right;
    return Handle::None;
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

ImageCanvas::Handle ImageCanvas::handleAt(const QPoint &pos) const {
    QRect r = selectionRect();
    if (r.isEmpty()) return Handle::None;
    const int t = 10; // grab tolerance in widget px
    auto near = [&](int a, int b) { return std::abs(a - b) <= t; };
    bool onLeft   = near(pos.x(), r.left());
    bool onRight  = near(pos.x(), r.right());
    bool onTop    = near(pos.y(), r.top());
    bool onBottom = near(pos.y(), r.bottom());
    // Only count edge hits when the other axis is within the rect span (± tol).
    bool inX = pos.x() >= r.left() - t && pos.x() <= r.right() + t;
    bool inY = pos.y() >= r.top() - t && pos.y() <= r.bottom() + t;
    if (onTop && onLeft)       return Handle::TopLeft;
    if (onTop && onRight)      return Handle::TopRight;
    if (onBottom && onLeft)    return Handle::BottomLeft;
    if (onBottom && onRight)   return Handle::BottomRight;
    if (onTop && inX)          return Handle::Top;
    if (onBottom && inX)       return Handle::Bottom;
    if (onLeft && inY)         return Handle::Left;
    if (onRight && inY)        return Handle::Right;
    return Handle::None;
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
    p.fillRect(rect(), m_backgroundColor);
    if (m_img.isNull()) {
        p.setPen(Qt::lightGray);
        p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        if (m_dragHighlight) {
            p.setPen(QPen(QColor(120, 200, 255), 3, Qt::DashLine));
            p.drawRect(rect().adjusted(2, 2, -2, -2));
        }
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

            // Rule-of-thirds gridlines, only while actively dragging.
            if (m_drag != Drag::None) {
                p.setPen(QPen(QColor(255, 255, 255, 90), 1));
                for (int i = 1; i <= 2; ++i) {
                    int x = sel.left() + sel.width() * i / 3;
                    int y = sel.top() + sel.height() * i / 3;
                    p.drawLine(x, sel.top(), x, sel.bottom());
                    p.drawLine(sel.left(), y, sel.right(), y);
                }
            }

            // Corner brackets (L-shapes) + edge ticks.
            const int leg = std::min(18, std::min(sel.width(), sel.height()) / 3);
            p.setPen(QPen(Qt::white, 2));
            const int l = sel.left(), t = sel.top(), r = sel.right(), b = sel.bottom();
            // corners
            p.drawLine(l, t, l + leg, t); p.drawLine(l, t, l, t + leg);
            p.drawLine(r, t, r - leg, t); p.drawLine(r, t, r, t + leg);
            p.drawLine(l, b, l + leg, b); p.drawLine(l, b, l, b - leg);
            p.drawLine(r, b, r - leg, b); p.drawLine(r, b, r, b - leg);
            // edge midpoint ticks
            int mx = (l + r) / 2, my = (t + b) / 2;
            p.drawLine(mx - leg / 2, t, mx + leg / 2, t);
            p.drawLine(mx - leg / 2, b, mx + leg / 2, b);
            p.drawLine(l, my - leg / 2, l, my + leg / 2);
            p.drawLine(r, my - leg / 2, r, my + leg / 2);
        }
    }

    if (m_marquee) {
        QRect box = QRect(m_mp0, m_mp1).normalized();
        p.setPen(QPen(QColor(120, 180, 255), 1, Qt::DashLine));
        p.setBrush(QColor(120, 180, 255, 40));
        p.drawRect(box);
    }

    if (m_healMode && underMouse()) {
        // Existing spots: reddish highlight, visible only while hovering.
        if (!m_healSpots.isEmpty()) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(255, 60, 60, 210), 2));
            p.setBrush(QColor(255, 60, 60, 60));
            for (const HealMarker &m : m_healSpots) {
                QPointF c = m_topLeft + QPointF(m.pos.x() * m_scale, m.pos.y() * m_scale);
                double r = m.radius * m_scale;
                p.drawEllipse(c, r, r);
            }
        }

        // Brush radius is in image(display) px; scale to on-screen size.
        double rad = m_brushRadius * m_scale;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawEllipse(QPointF(m_mousePos), rad, rad);
    }

    if (m_eraseMode && underMouse()) {
        double rad = m_brushRadius * m_scale;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 90, 90, 220), 1));
        p.setBrush(QColor(255, 60, 60, 40));
        p.drawEllipse(QPointF(m_mousePos), rad, rad);
    }

    // Local-mask gizmo.
    if (m_maskMode && m_hasActiveMask) {
        const double W = m_img.width();
        auto toScreen = [&](const QPointF &n) {
            return m_topLeft + QPointF(n.x() * W * m_scale, n.y() * W * m_scale);
        };
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor line(120, 200, 255);
        const Mask &m = m_activeMask;
        if (m.type == MaskType::Radial) {
            QPointF c = toScreen(m.center);
            double rx = m.radiusX * W * m_scale, ry = m.radiusY * W * m_scale;
            p.save();
            p.translate(c);
            p.rotate(m.angle * 180.0 / M_PI);
            p.setPen(QPen(line, 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(0, 0), rx, ry);
            p.restore();
            p.setPen(QPen(line, 1));
            p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
            p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
        } else if (m.type == MaskType::Linear) {
            QPointF a = toScreen(m.p0), b = toScreen(m.p1);
            QPointF d = b - a;
            double len = std::hypot(d.x(), d.y());
            QPointF perp = len > 1e-3 ? QPointF(-d.y() / len, d.x() / len)
                                      : QPointF(0, 0);
            p.setPen(QPen(line, 1.5));
            p.drawLine(a, b);
            p.setPen(QPen(line, 2)); // solid tick at full-effect end (p0)
            p.drawLine(a - perp * 30, a + perp * 30);
            p.setPen(QPen(line, 1, Qt::DashLine)); // dashed at zero end (p1)
            p.drawLine(b - perp * 30, b + perp * 30);
            p.setBrush(line);
            p.drawEllipse(a, 3, 3);
            p.drawEllipse(b, 3, 3);
        } else { // Brush: show painted coverage plus the brush cursor
            if (!m_maskOverlay.isNull())
                p.drawImage(tr, m_maskOverlay);
            double rad = m.brushRadius * W * m_scale;
            // While Alt is held the brush erases instead of paints; tint the
            // cursor red so that's obvious before the user clicks.
            if (m_maskErasing) {
                p.setPen(QPen(QColor(255, 90, 90, 220), 1));
                p.setBrush(QColor(255, 60, 60, 40));
            } else {
                p.setPen(QPen(QColor(255, 255, 255, 200), 1));
                p.setBrush(QColor(120, 200, 255, 30));
            }
            p.drawEllipse(QPointF(m_mousePos), rad, rad);
        }
    }

    // Targeted color-range drag feedback: a swatch of the picked color (border
    // tinted by the channel being adjusted) plus a centered-zero amount bar.
    if (m_colorRangeDragging) {
        const int sw = 22;
        QPoint tl = m_colorRangeStart + QPoint(14, -14 - sw);
        tl.setX(std::clamp(tl.x(), 2, width() - sw - 2));
        tl.setY(std::clamp(tl.y(), 2, height() - sw - 10));
        const QRect swatch(tl, QSize(sw, sw));
        static const QColor chColors[3] = {QColor(235, 80, 80),
                                           QColor(80, 200, 80),
                                           QColor(90, 130, 255)};
        p.setPen(QPen(QColor(0, 0, 0, 180), 1));
        p.setBrush(m_colorRangeColor);
        p.drawRoundedRect(swatch.adjusted(-1, -1, 1, 1), 4, 4);
        p.setPen(QPen(chColors[std::clamp(m_colorRangeChannel, 0, 2)], 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(swatch, 4, 4);
        // Amount bar: fill grows from the centre, right for +, left for -.
        const QRect bar(swatch.left() - 8, swatch.bottom() + 4, sw + 16, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRoundedRect(bar, 2, 2);
        const int cx = bar.center().x();
        const int half = bar.width() / 2 - 1;
        const int len = int(std::lround(half * std::abs(m_colorRangeAmount) / 100.0));
        if (len > 0) {
            QRect fill = m_colorRangeAmount > 0
                             ? QRect(cx, bar.top(), len, bar.height())
                             : QRect(cx - len, bar.top(), len, bar.height());
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawRoundedRect(fill, 2, 2);
        }
        p.setPen(QPen(QColor(255, 255, 255, 180), 1));
        p.drawLine(cx, bar.top() - 1, cx, bar.bottom() + 1);
    }

    if (m_hasActiveImageLayer) {
        QRectF frImg = imageLayerFrameRect();
        if (!frImg.isEmpty()) {
            QRect fr = QRectF(m_topLeft + QPointF(frImg.left() * m_scale, frImg.top() * m_scale),
                              QSizeF(frImg.width() * m_scale, frImg.height() * m_scale))
                          .toRect();
            // Same look as the crop tool's selection gizmo: a dashed outline
            // plus L-shaped corner brackets and edge midpoint ticks, sized
            // proportionally to the frame (capped at 18px) instead of fixed
            // filled squares — no separate handle geometry to keep in sync.
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawRect(fr);
            const int leg = std::min(18, std::min(fr.width(), fr.height()) / 3);
            p.setPen(QPen(Qt::white, 2));
            const int l = fr.left(), t = fr.top(), r = fr.right(), b = fr.bottom();
            p.drawLine(l, t, l + leg, t); p.drawLine(l, t, l, t + leg);
            p.drawLine(r, t, r - leg, t); p.drawLine(r, t, r, t + leg);
            p.drawLine(l, b, l + leg, b); p.drawLine(l, b, l, b - leg);
            p.drawLine(r, b, r - leg, b); p.drawLine(r, b, r, b - leg);
            int mx = (l + r) / 2, my = (t + b) / 2;
            p.drawLine(mx - leg / 2, t, mx + leg / 2, t);
            p.drawLine(mx - leg / 2, b, mx + leg / 2, b);
            p.drawLine(l, my - leg / 2, l, my + leg / 2);
            p.drawLine(r, my - leg / 2, r, my + leg / 2);
        }
    }

    if (m_dragHighlight) {
        p.setPen(QPen(QColor(120, 200, 255), 3, Qt::DashLine));
        p.drawRect(rect().adjusted(2, 2, -2, -2));
    }
}

// ---- Mouse -----------------------------------------------------------------

void ImageCanvas::mousePressEvent(QMouseEvent *ev) {
    if (m_img.isNull()) return;

    // Map a widget point to image-pixel coords (or QPoint(-1,-1) if outside).
    auto imagePointAt = [this](const QPoint &pos) -> QPoint {
        QRect tr = targetRect();
        if (!tr.contains(pos)) return QPoint(-1, -1);
        double sx = double(m_img.width()) / tr.width();
        double sy = double(m_img.height()) / tr.height();
        return QPoint(std::clamp(int((pos.x() - tr.x()) * sx), 0, m_img.width() - 1),
                      std::clamp(int((pos.y() - tr.y()) * sy), 0, m_img.height() - 1));
    };

    // White-balance eyedropper.
    if (m_pickMode && ev->button() == Qt::LeftButton) {
        QPoint ip = imagePointAt(ev->pos());
        if (ip.x() >= 0) emit colorPicked(m_img.pixelColor(ip.x(), ip.y()));
        return;
    }

    // Targeted color-range tool: sample the pixel, then track a horizontal drag.
    if (m_colorRangeMode && ev->button() == Qt::LeftButton) {
        QPoint ip = imagePointAt(ev->pos());
        if (ip.x() >= 0) {
            const QColor c = m_img.pixelColor(ip.x(), ip.y());
            m_colorRangeDragging = true;
            m_colorRangeStart = ev->pos();
            m_colorRangeColor = c;
            m_colorRangeChannel = (c.green() >= c.red() && c.green() >= c.blue())
                                      ? 1
                                      : (c.red() >= c.blue() ? 0 : 2);
            m_colorRangeAmount = 0;
            setCursor(Qt::SizeHorCursor);
            emit colorRangePickStarted(c);
            update();
        }
        return;
    }

    // Spot-heal brush: each click places one heal spot.
    if (m_healMode && ev->button() == Qt::LeftButton) {
        QPoint ip = imagePointAt(ev->pos());
        if (ip.x() >= 0) emit healAt(ip);
        return;
    }

    // Local-mask editing: a drag defines the active mask's geometry.
    if (m_maskMode && ev->button() == Qt::LeftButton) {
        QPointF n = normPointAt(ev->pos());
        m_maskDragging = true;
        m_maskCenterNorm = n;
        m_mousePos = ev->pos();
        m_maskErasing = ev->modifiers().testFlag(Qt::AltModifier);
        if (m_maskKind == MaskType::Radial)
            emit maskRadialDragged(n, 0.0);
        else if (m_maskKind == MaskType::Linear)
            emit maskLinearDragged(n, n);
        else {
            m_lastBrushNorm = n;
            emit maskBrushPoint(n, m_maskErasing);
        }
        update();
        return;
    }

    // Erase brush: only active while an image layer is selected.
    if (m_eraseMode && m_hasActiveImageLayer && ev->button() == Qt::LeftButton) {
        QPointF n = normPointAt(ev->pos());
        m_eraseDragging = true;
        m_lastEraseNorm = n;
        m_mousePos = ev->pos();
        emit eraseAt(n);
        update();
        return;
    }

    // Crop mode.
    if (m_cropMode && ev->button() == Qt::LeftButton) {
        Handle h = handleAt(ev->pos());
        if (h != Handle::None) {
            m_drag = Drag::Resizing;
            m_activeHandle = h;
            m_rectAtDragStart = selectionRect();
        } else if (selectionRect().contains(ev->pos())) {
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

    if (m_hasActiveImageLayer && ev->button() == Qt::LeftButton &&
        !m_zoomMode && !m_spaceDown) {
        Handle h = imageLayerHandleAt(ev->pos());
        QRectF fr = imageLayerFrameRect();
        QPointF imgPos = (QPointF(ev->pos()) - m_topLeft) / m_scale;
        if (h != Handle::None) {
            m_imageDragging = true;
            m_imageActiveHandle = h;
            m_imageMoveStart = ev->pos();
            m_imageFrameAtDragStart = fr;
            m_imageOffsetAtDragStart = m_activeMask.sourceImageOffset;
            m_imageScaleAtDragStart = m_activeMask.sourceImageScale;
            m_imageDragEmitThrottle.start();
            setCursor((h == Handle::TopLeft || h == Handle::BottomRight) ? Qt::SizeFDiagCursor :
                      (h == Handle::TopRight || h == Handle::BottomLeft) ? Qt::SizeBDiagCursor :
                      (h == Handle::Top || h == Handle::Bottom) ? Qt::SizeVerCursor :
                      (h == Handle::Left || h == Handle::Right) ? Qt::SizeHorCursor :
                      Qt::SizeAllCursor);
            update();
            return;
        }
        if (fr.contains(imgPos)) {
            m_imageDragging = true;
            m_imageActiveHandle = Handle::None;
            m_imageMoveStart = ev->pos();
            m_imageFrameAtDragStart = fr;
            m_imageOffsetAtDragStart = m_activeMask.sourceImageOffset;
            m_imageScaleAtDragStart = m_activeMask.sourceImageScale;
            m_imageDragEmitThrottle.start();
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
    }

    // Normal mode: Space+drag always pans; plain drag draws a zoom marquee
    // only while the Zoom tool is selected.
    if (ev->button() == Qt::LeftButton) {
        if (m_spaceDown) {
            m_panning = true;
            m_panLast = ev->pos();
            setCursor(Qt::ClosedHandCursor);
        } else if (m_zoomMode) {
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
    if (m_colorRangeDragging) {
        emit colorRangeDragged(ev->pos().x() - m_colorRangeStart.x());
        update();
        return;
    }
    if (m_maskMode) {
        m_mousePos = ev->pos();
        if (m_maskDragging) {
            QPointF n = normPointAt(ev->pos());
            if (m_maskKind == MaskType::Radial) {
                double dx = n.x() - m_maskCenterNorm.x();
                double dy = n.y() - m_maskCenterNorm.y();
                emit maskRadialDragged(m_maskCenterNorm, std::sqrt(dx * dx + dy * dy));
            } else if (m_maskKind == MaskType::Linear) {
                emit maskLinearDragged(m_maskCenterNorm, n);
            } else {
                double dx = n.x() - m_lastBrushNorm.x();
                double dy = n.y() - m_lastBrushNorm.y();
                if (dx * dx + dy * dy > 0.004 * 0.004) { // throttle stroke samples
                    m_lastBrushNorm = n;
                    m_maskErasing = ev->modifiers().testFlag(Qt::AltModifier);
                    emit maskBrushPoint(n, m_maskErasing);
                }
            }
        } else if (m_maskKind == MaskType::Brush) {
            m_maskErasing = ev->modifiers().testFlag(Qt::AltModifier);
        }
        update();
        return;
    }
    if (m_eraseMode) {
        m_mousePos = ev->pos();
        if (m_eraseDragging) {
            QPointF n = normPointAt(ev->pos());
            double dx = n.x() - m_lastEraseNorm.x();
            double dy = n.y() - m_lastEraseNorm.y();
            if (dx * dx + dy * dy > 0.004 * 0.004) { // throttle stroke samples
                m_lastEraseNorm = n;
                emit eraseAt(n);
            }
        }
        update();
        return;
    }
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
    } else if (m_drag == Drag::Resizing) {
        QRect tr = targetRect();
        QRect r = m_rectAtDragStart;
        QPoint pos = ev->pos();
        // Move the edge(s) owned by the active handle to follow the cursor,
        // clamped to the image bounds.
        int L = r.left(), T = r.top(), R = r.right(), B = r.bottom();
        auto cx = [&](int x) { return std::clamp(x, tr.left(), tr.right()); };
        auto cy = [&](int y) { return std::clamp(y, tr.top(), tr.bottom()); };
        switch (m_activeHandle) {
            case Handle::Left:        L = cx(pos.x()); break;
            case Handle::Right:       R = cx(pos.x()); break;
            case Handle::Top:         T = cy(pos.y()); break;
            case Handle::Bottom:      B = cy(pos.y()); break;
            case Handle::TopLeft:     L = cx(pos.x()); T = cy(pos.y()); break;
            case Handle::TopRight:    R = cx(pos.x()); T = cy(pos.y()); break;
            case Handle::BottomLeft:  L = cx(pos.x()); B = cy(pos.y()); break;
            case Handle::BottomRight: R = cx(pos.x()); B = cy(pos.y()); break;
            case Handle::None:        break;
        }
        QRect nr = QRect(QPoint(L, T), QPoint(R, B)).normalized();

        if (m_cropAspect > 0) {
            // Preserve aspect: anchor the corner opposite the moving one and
            // reuse constrainedCorner (which anchors at m_p0). Edge handles are
            // treated as their adjacent "grow" corner.
            QPoint anchor, moving;
            switch (m_activeHandle) {
                case Handle::TopLeft:     anchor = r.bottomRight(); moving = nr.topLeft(); break;
                case Handle::TopRight:    anchor = r.bottomLeft();  moving = nr.topRight(); break;
                case Handle::BottomLeft:  anchor = r.topRight();    moving = nr.bottomLeft(); break;
                case Handle::BottomRight: anchor = r.topLeft();     moving = nr.bottomRight(); break;
                case Handle::Left:        anchor = r.bottomRight(); moving = QPoint(nr.left(), nr.top()); break;
                case Handle::Right:       anchor = r.topLeft();     moving = QPoint(nr.right(), nr.bottom()); break;
                case Handle::Top:         anchor = r.bottomRight(); moving = QPoint(nr.left(), nr.top()); break;
                case Handle::Bottom:      anchor = r.topLeft();     moving = QPoint(nr.right(), nr.bottom()); break;
                case Handle::None:        anchor = r.topLeft();     moving = nr.bottomRight(); break;
            }
            QPoint savedP0 = m_p0;
            m_p0 = anchor;                       // constrainedCorner anchors at m_p0
            QPoint c = constrainedCorner(moving);
            m_p0 = savedP0;
            nr = QRect(anchor, c).normalized();
        }

        m_p0 = nr.topLeft();
        m_p1 = nr.bottomRight();
        update();
    } else if (m_imageDragging) {
        QPointF delta = (QPointF(ev->pos()) - m_imageMoveStart) / m_scale;
        const double cw = std::max(1, m_img.width());
        const double ch = std::max(1, m_img.height());
        QRectF fr = m_imageFrameAtDragStart;
        // Throttle the (expensive, model-triggering) signal to ~60fps; the
        // frame/handles below are updated on m_activeMask directly on every
        // move regardless, so the gizmo always tracks the cursor exactly —
        // only the underlying re-render lags slightly behind on fast drags.
        const bool emitNow = !m_imageDragEmitThrottle.isValid() ||
                             m_imageDragEmitThrottle.elapsed() >= 16;
        if (m_imageActiveHandle == Handle::None) {
            fr.translate(delta);
            QPointF center = fr.center();
            QPointF offset(std::clamp((center.x() / cw - 0.5) * 2.0, -1.0, 1.0),
                           std::clamp((center.y() / ch - 0.5) * 2.0, -1.0, 1.0));
            m_activeMask.sourceImageOffset = offset;
            if (emitNow) {
                emit imageLayerTransformChanged(offset, m_imageScaleAtDragStart,
                                                m_activeMask.sourceImageLockRatio);
                m_imageDragEmitThrottle.restart();
            }
        } else {
            double L = fr.left(), T = fr.top(), R = fr.right(), B = fr.bottom();
            auto clampX = [&](double x) { return std::clamp(x, 0.0, double(cw)); };
            auto clampY = [&](double y) { return std::clamp(y, 0.0, double(ch)); };
            switch (m_imageActiveHandle) {
            case Handle::Left:        L = clampX(L + delta.x()); break;
            case Handle::Right:       R = clampX(R + delta.x()); break;
            case Handle::Top:         T = clampY(T + delta.y()); break;
            case Handle::Bottom:      B = clampY(B + delta.y()); break;
            case Handle::TopLeft:     L = clampX(L + delta.x()); T = clampY(T + delta.y()); break;
            case Handle::TopRight:    R = clampX(R + delta.x()); T = clampY(T + delta.y()); break;
            case Handle::BottomLeft:  L = clampX(L + delta.x()); B = clampY(B + delta.y()); break;
            case Handle::BottomRight: R = clampX(R + delta.x()); B = clampY(B + delta.y()); break;
            case Handle::None: break;
            }
            QRectF nr(QPointF(L, T), QPointF(R, B));
            if (nr.width() < 8.0 || nr.height() < 8.0) return;
            const bool lock = m_activeMask.sourceImageLockRatio;
            QPointF scale(std::clamp(nr.width() / cw, kImageLayerScaleMin, kImageLayerScaleMax),
                          std::clamp(nr.height() / ch, kImageLayerScaleMin, kImageLayerScaleMax));
            if (lock) {
                double s = scale.x();
                if (m_imageActiveHandle == Handle::Top || m_imageActiveHandle == Handle::Bottom)
                    s = scale.y();
                else if (m_imageActiveHandle == Handle::Left || m_imageActiveHandle == Handle::Right)
                    s = scale.x();
                else
                    s = std::max(scale.x(), scale.y());
                s = std::clamp(s, kImageLayerScaleMin, kImageLayerScaleMax);
                scale = QPointF(s, s);
                nr = QRectF(nr.center() - QPointF(cw * s / 2.0, ch * s / 2.0),
                            QSizeF(cw * s, ch * s));
            }
            QPointF center = nr.center();
            QPointF offset(std::clamp((center.x() / cw - 0.5) * 2.0, -1.0, 1.0),
                           std::clamp((center.y() / ch - 0.5) * 2.0, -1.0, 1.0));
            m_activeMask.sourceImageOffset = offset;
            m_activeMask.sourceImageScale = scale;
            if (emitNow) {
                emit imageLayerTransformChanged(offset, scale, lock);
                m_imageDragEmitThrottle.restart();
            }
        }
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
        Handle h = handleAt(ev->pos());
        Qt::CursorShape c = Qt::CrossCursor;
        switch (h) {
            case Handle::TopLeft:
            case Handle::BottomRight: c = Qt::SizeFDiagCursor; break;
            case Handle::TopRight:
            case Handle::BottomLeft:  c = Qt::SizeBDiagCursor; break;
            case Handle::Top:
            case Handle::Bottom:      c = Qt::SizeVerCursor; break;
            case Handle::Left:
            case Handle::Right:       c = Qt::SizeHorCursor; break;
            case Handle::None:
                c = selectionRect().contains(ev->pos()) ? Qt::SizeAllCursor
                                                        : Qt::CrossCursor;
                break;
        }
        setCursor(c);
    } else if (m_healMode) {
        m_mousePos = ev->pos();
        update(); // move the brush-size circle
    } else if (m_hasActiveImageLayer) {
        Handle h = imageLayerHandleAt(ev->pos());
        Qt::CursorShape c = Qt::ArrowCursor;
        switch (h) {
        case Handle::TopLeft:
        case Handle::BottomRight: c = Qt::SizeFDiagCursor; break;
        case Handle::TopRight:
        case Handle::BottomLeft:  c = Qt::SizeBDiagCursor; break;
        case Handle::Top:
        case Handle::Bottom:      c = Qt::SizeVerCursor; break;
        case Handle::Left:
        case Handle::Right:       c = Qt::SizeHorCursor; break;
        case Handle::None:
            c = imageLayerFrameRect().contains((QPointF(ev->pos()) - m_topLeft) / m_scale)
                    ? Qt::SizeAllCursor
                    : Qt::ArrowCursor;
            break;
        }
        setCursor(c);
    }
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *ev) {
    if (m_colorRangeDragging && ev->button() == Qt::LeftButton) {
        m_colorRangeDragging = false;
        setCursor(m_colorRangeMode ? pipetteCursor() : Qt::ArrowCursor);
        emit colorRangeReleased();
        update();
        return;
    }
    if (m_maskDragging && ev->button() == Qt::LeftButton) {
        m_maskDragging = false;
        emit maskEditFinished();
        update();
        return;
    }
    if (m_eraseDragging && ev->button() == Qt::LeftButton) {
        m_eraseDragging = false;
        emit eraseFinished();
        update();
        return;
    }
    if (m_imageDragging && ev->button() == Qt::LeftButton) {
        m_imageDragging = false;
        m_imageActiveHandle = Handle::None;
        setCursor(Qt::ArrowCursor);
        // Commit the final position/size if the drag actually moved anything
        // and the last move's emit was throttled away, so the model always
        // ends up matching the gizmo (a plain click with no movement must
        // not mark the document dirty).
        if (m_activeMask.sourceImageOffset != m_imageOffsetAtDragStart ||
            m_activeMask.sourceImageScale != m_imageScaleAtDragStart) {
            emit imageLayerTransformChanged(m_activeMask.sourceImageOffset,
                                            m_activeMask.sourceImageScale,
                                            m_activeMask.sourceImageLockRatio);
        }
        update();
        return;
    }
    if (m_drag != Drag::None && ev->button() == Qt::LeftButton) {
        m_drag = Drag::None;
        m_activeHandle = Handle::None;
        setCursor(m_cropMode ? Qt::CrossCursor : Qt::ArrowCursor);
        update();
        emit cropSelected(selectionInImage());
        return;
    }
    if (m_panning && (ev->button() == Qt::LeftButton || ev->button() == Qt::MiddleButton)) {
        m_panning = false;
        if (m_spaceDown) setCursor(Qt::OpenHandCursor);
        else if (m_zoomMode) setCursor(zoomCursor());
        else setCursor(Qt::ArrowCursor);
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
        // In heal mode, ctrl+wheel resizes the brush instead of zooming.
        if (m_healMode) {
            int step = ev->angleDelta().y() > 0 ? 2 : -2;
            m_brushRadius = std::clamp(m_brushRadius + step, kHealBrushMin, kHealBrushMax);
            emit healBrushRadiusChanged(m_brushRadius);
            update();
            ev->accept();
            return;
        }
        // In erase mode, ctrl+wheel resizes the erase brush the same way.
        if (m_eraseMode) {
            int step = ev->angleDelta().y() > 0 ? 2 : -2;
            m_brushRadius = std::clamp(m_brushRadius + step, kHealBrushMin, kHealBrushMax);
            emit eraseBrushRadiusChanged(m_brushRadius);
            update();
            ev->accept();
            return;
        }
        // In brush-mask mode, ctrl+wheel resizes the mask brush the same way.
        if (m_maskMode && (m_maskKind == MaskType::Brush || m_maskKind == MaskType::Paint)) {
            double step = ev->angleDelta().y() > 0 ? kMaskBrushStep : -kMaskBrushStep;
            double r = std::clamp(m_activeMask.brushRadius + step, kMaskBrushMin, kMaskBrushMax);
            emit maskBrushRadiusChanged(r);
            update();
            ev->accept();
            return;
        }
        // Ctrl+wheel only zooms while the Zoom tool is selected.
        if (m_zoomMode) {
            double f = ev->angleDelta().y() > 0 ? 1.10 : (1.0 / 1.10);
            zoomTo(m_scale * f, ev->position());
            ev->accept();
            return;
        }
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
        if (!m_panning)
            setCursor(m_cropMode ? Qt::CrossCursor
                                 : (m_zoomMode ? zoomCursor() : QCursor(Qt::ArrowCursor)));
        ev->accept();
        return;
    }
    QWidget::keyReleaseEvent(ev);
}

void ImageCanvas::leaveEvent(QEvent *) {
    // Hide the brush cursor and spot-heal overlay once the mouse leaves.
    if (m_healMode) update();
}

void ImageCanvas::dragEnterEvent(QDragEnterEvent *ev) {
    if (hasLocalFileUrl(ev->mimeData())) {
        ev->acceptProposedAction();
        m_dragHighlight = true;
        update();
    }
}

void ImageCanvas::dragLeaveEvent(QDragLeaveEvent *) {
    m_dragHighlight = false;
    update();
}

void ImageCanvas::dropEvent(QDropEvent *ev) {
    m_dragHighlight = false;
    update();
    if (!hasLocalFileUrl(ev->mimeData())) return;
    for (const QUrl &u : ev->mimeData()->urls()) {
        if (u.isLocalFile()) {
            ev->acceptProposedAction();
            emit imageLayerDropped(u.toLocalFile());
            return; // one layer per drop
        }
    }
}

void ImageCanvas::setBackgroundColor(const QColor &color) {
    if (!color.isValid() || color == m_backgroundColor) return;
    m_backgroundColor = color;
    update();
    emit backgroundColorChanged(m_backgroundColor);
}

void ImageCanvas::contextMenuEvent(QContextMenuEvent *ev) {
    static const QColor kDefaultBackground(30, 30, 30);
    QMenu menu(this);
    QAction *black = menu.addAction(tr("Black"));
    QAction *white = menu.addAction(tr("White"));
    QAction *gray = menu.addAction(tr("Gray"));
    menu.addSeparator();
    QAction *custom = menu.addAction(tr("Custom..."));
    QAction *reset = menu.addAction(tr("Reset to Default"));

    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == black) {
        setBackgroundColor(Qt::black);
    } else if (chosen == white) {
        setBackgroundColor(Qt::white);
    } else if (chosen == gray) {
        setBackgroundColor(QColor(0x80, 0x80, 0x80));
    } else if (chosen == custom) {
        QColor c = QColorDialog::getColor(m_backgroundColor, this, tr("Canvas Background Color"));
        if (c.isValid()) setBackgroundColor(c);
    } else if (chosen == reset) {
        setBackgroundColor(kDefaultBackground);
    }
}
