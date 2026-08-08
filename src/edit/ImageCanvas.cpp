#include "edit/ImageCanvas.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QMenu>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QPixmap>
#include <QPainterPath>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QColorDialog>
#include <QPlainTextEdit>
#include <QFont>
#include <QTextCursor>
#include <QTransform>
#include <QPolygonF>
#include <QLineF>
#include <QEvent>
#include <QSettings>
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
constexpr double kMaskBrushStep = 0.005;
// Per-notch brush-size step for ctrl+wheel resizing (one "notch" = the
// standard 120 angleDelta units a physical mouse wheel reports). Scaling by
// the actual delta rather than applying a full step per wheel event keeps
// high-resolution mice/trackpads — which report many small deltas per
// gesture — from blowing through the size range almost instantly.
constexpr double kHealBrushStepPerNotch = 1.0;
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

// A paint-bucket cursor for the flood-fill tool: a tilted bucket silhouette
// with a paint droplet falling from the spout, tip pointing down-left to
// where the fill originates.
const QCursor &bucketCursor() {
    static const QCursor c = [] {
        constexpr int px = 32;
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Build the bucket in local space, then rotate to a natural pour angle.
        QPainterPath body;
        body.moveTo(-9, -6);
        body.lineTo(9, -6);
        body.lineTo(6, 8);
        body.lineTo(-6, 8);
        body.closeSubpath();

        QPainterPath handle;
        handle.addRoundedRect(QRectF(-6, -12, 12, 8), 5, 5);

        QTransform t;
        t.translate(15, 10);
        t.rotate(-30);
        QPainterPath outline = t.map(body);
        QPainterPath handleOutline = t.map(handle);

        p.setPen(QPen(Qt::white, 2.2));
        p.setBrush(Qt::black);
        p.drawPath(outline);

        p.setBrush(Qt::NoBrush);
        p.drawPath(handleOutline);

        // Paint droplet falling from the spout toward the cursor tip.
        QPainterPath drop;
        drop.moveTo(-1.5, 0);
        drop.cubicTo(-3, 3, -3, 6, 0, 6);
        drop.cubicTo(3, 6, 3, 3, 1.5, 0);
        drop.closeSubpath();
        QTransform dt;
        dt.translate(3, 24);
        p.setPen(QPen(Qt::white, 1.4));
        p.setBrush(Qt::black);
        p.drawPath(dt.map(drop));

        p.end();
        return QCursor(pm, 2, 30);
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
    QSettings settings;
    m_backgroundColor = settings.value("edit/canvasBackgroundColor",
                                       QColor(30, 30, 30)).value<QColor>();
    pal.setColor(QPalette::Window, m_backgroundColor);
    setPalette(pal);
    m_showRulers = settings.value("canvas/showRulers", false).toBool();
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

void ImageCanvas::setBucketMode(bool on) {
    m_bucketMode = on;
    if (on) setCursor(bucketCursor());
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

void ImageCanvas::setTextMode(bool on) {
    m_textMode = on;
    if (!on) {
        m_textDrag = TextDrag::None;
        m_activeTextIndex = -1;
        cancelTextEditor();
    }
    setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setShapeMode(bool on) {
    m_shapeMode = on;
    if (!on) {
        m_shapeDrag = ShapeDrag::None;
        m_activeShapeIndex = -1;
    }
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setActiveShapeType(ShapeType t) {
    m_activeShapeType = t;
}

void ImageCanvas::setShapeMarkers(const QVector<ShapeMarker> &markers) {
    m_shapeMarkers = markers;
    if (m_activeShapeIndex >= m_shapeMarkers.size()) m_activeShapeIndex = -1;
    if (m_shapeMode) update();
}

void ImageCanvas::setActiveShapeIndex(int index) {
    m_activeShapeIndex = index;
    if (m_shapeMode) update();
}

void ImageCanvas::setSelectedShapeIndices(const QSet<int> &indices) {
    m_selectedShapeIndices = indices;
    if (m_shapeMode) update();
}

void ImageCanvas::setTextMarkers(const QVector<TextMarker> &markers) {
    m_textMarkers = markers;
    if (m_activeTextIndex >= m_textMarkers.size()) m_activeTextIndex = -1;
    if (m_textMode) update();
}

void ImageCanvas::setPaintMarkers(const QVector<QRectF> &markers) {
    m_paintMarkers = markers;
}

void ImageCanvas::setImageLayerMarkers(const QVector<QRectF> &markers) {
    m_imageLayerMarkers = markers;
}

void ImageCanvas::setActiveTextIndex(int index) {
    m_activeTextIndex = index;
    if (m_textMode) update();
}

void ImageCanvas::beginTextEdit(int index, const QPointF &imgPos, const QFont &baseFont,
                                const QColor &color, const QString &initialText) {
    if (!m_textEditor) {
        m_textEditor = new QPlainTextEdit(this);
        m_textEditor->setFrameStyle(QFrame::NoFrame);
        m_textEditor->setAttribute(Qt::WA_TranslucentBackground);
        m_textEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->document()->setDocumentMargin(0);
        m_textEditor->installEventFilter(this);
        connect(m_textEditor, &QPlainTextEdit::textChanged, this, [this] {
            if (m_textEditIndex >= 0)
                emit textLiveContentChanged(m_textEditIndex, m_textEditor->toPlainText());
        });
    }
    m_textEditIndex = index;
    // `baseFont`'s pixel size is in display-image pixels (RetouchTab scales
    // by m_scaleFromGeom, independent of the canvas's own view zoom). The
    // widget itself lives in on-screen widget pixels, so it also needs the
    // current view zoom (m_scale) applied — otherwise, at any zoom level
    // other than 100%, the editor's text renders at the wrong size relative
    // to the actual composited text underneath (e.g. huge when zoomed out).
    QFont font = baseFont;
    font.setPixelSize(std::max(1, int(std::lround(baseFont.pixelSize() * m_scale))));
    m_textEditor->setFont(font);
    QPalette pal = m_textEditor->palette();
    pal.setColor(QPalette::Text, color);
    // Fully transparent — no background wash, so the box reads as a caret
    // over the live text rather than a highlighted panel around it (the
    // underlying baked-in copy of this same op is suppressed for the
    // duration of the edit — see RetouchTab::onRenderDone — so there's no
    // double-text ghosting either).
    pal.setColor(QPalette::Base, Qt::transparent);
    m_textEditor->setPalette(pal);
    m_textEditor->setPlainText(initialText);
    QPoint wp = (m_topLeft + imgPos * m_scale).toPoint();
    m_textEditor->move(wp);

    // Size the box to fit the actual text tightly (plus a little room to
    // keep typing), not a fixed fraction of the canvas — otherwise long/
    // existing text gets clipped, or the box reads as a big empty panel
    // around a small piece of text.
    QFontMetricsF fm(font);
    const QStringList lines = initialText.isEmpty() ? QStringList{QString()}
                                                     : initialText.split(QLatin1Char('\n'));
    double textWidth = 0.0;
    for (const QString &line : lines)
        textWidth = std::max(textWidth, double(fm.horizontalAdvance(line)));
    int w = std::clamp(int(textWidth) + 24, 40, std::max(40, width() - wp.x() - 8));
    int h = std::max(int(fm.height()), int(fm.lineSpacing() * (lines.size() - 1) + fm.height()));
    m_textEditor->resize(w, h);
    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->setFocus();
    // Cursor at the end (ready to keep typing), no selection — since the box
    // is now sized to fit the whole line, this doesn't scroll anything out
    // of view (unlike selecting the whole document, which jumps the view to
    // the end of the selection and hides the start of long text).
    QTextCursor c = m_textEditor->textCursor();
    c.movePosition(QTextCursor::End);
    m_textEditor->setTextCursor(c);
}

void ImageCanvas::commitTextEditor() {
    if (!m_textEditor || m_textEditIndex < 0) return;
    int idx = m_textEditIndex;
    QString text = m_textEditor->toPlainText();
    m_textEditIndex = -1;
    m_textEditor->hide();
    emit textEditCommitted(idx, text);
}

void ImageCanvas::cancelTextEditor() {
    if (!m_textEditor || m_textEditIndex < 0) return;
    int idx = m_textEditIndex;
    m_textEditIndex = -1;
    m_textEditor->hide();
    emit textEditCancelled(idx);
}

int ImageCanvas::textMarkerAt(const QPoint &pos) const {
    for (int i = m_textMarkers.size() - 1; i >= 0; --i) {
        const TextMarker &m = m_textMarkers[i];
        QPointF imgPos = (QPointF(pos) - m_topLeft) / m_scale;
        QPointF center = m.rect.topLeft();
        // Undo the marker's rotation about its anchor to test in local space.
        QTransform t;
        t.translate(center.x(), center.y());
        t.rotate(-m.rotation);
        t.translate(-center.x(), -center.y());
        QPointF local = t.map(imgPos);
        if (m.rect.contains(local)) return i;
    }
    return -1;
}

QPointF ImageCanvas::textRotateHandlePos(const TextMarker &m) const {
    QPointF anchor = m.rect.topLeft();
    QPointF handleImg(m.rect.center().x(), m.rect.top() - 28.0 / std::max(0.01, m_scale));
    QTransform t;
    t.translate(anchor.x(), anchor.y());
    t.rotate(m.rotation);
    t.translate(-anchor.x(), -anchor.y());
    QPointF rotated = t.map(handleImg);
    return m_topLeft + rotated * m_scale;
}

QPointF ImageCanvas::textCornerLocal(const TextMarker &m, Handle corner) const {
    switch (corner) {
    case Handle::TopRight:    return QPointF(m.rect.right(), m.rect.top());
    case Handle::BottomLeft:  return QPointF(m.rect.left(), m.rect.bottom());
    case Handle::BottomRight: return m.rect.bottomRight();
    default:                  return m.rect.topLeft();
    }
}

QPointF ImageCanvas::textCornerScreenPos(const TextMarker &m, Handle corner) const {
    QPointF anchor = m.rect.topLeft();
    QPointF local = textCornerLocal(m, corner);
    QTransform t;
    t.translate(anchor.x(), anchor.y());
    t.rotate(m.rotation);
    t.translate(-anchor.x(), -anchor.y());
    return m_topLeft + t.map(local) * m_scale;
}

ImageCanvas::Handle ImageCanvas::textCornerHandleAt(const QPoint &pos) const {
    if (m_activeTextIndex < 0 || m_activeTextIndex >= m_textMarkers.size()) return Handle::None;
    const TextMarker &m = m_textMarkers[m_activeTextIndex];
    const double t = 10.0;
    for (Handle h : {Handle::TopRight, Handle::BottomLeft, Handle::BottomRight}) {
        if ((QPointF(pos) - textCornerScreenPos(m, h)).manhattanLength() <= t) return h;
    }
    return Handle::None;
}

int ImageCanvas::shapeMarkerAt(const QPoint &pos) const {
    for (int i = m_shapeMarkers.size() - 1; i >= 0; --i) {
        const ShapeMarker &m = m_shapeMarkers[i];
        QPointF imgPos = (QPointF(pos) - m_topLeft) / m_scale;
        if (m.type == ShapeType::Line) {
            QLineF line(m.p1, m.p2);
            double t = std::clamp(QPointF::dotProduct(imgPos - m.p1, m.p2 - m.p1) /
                                       std::max(1.0, std::pow(line.length(), 2)),
                                   0.0, 1.0);
            QPointF closest = m.p1 + t * (m.p2 - m.p1);
            if ((imgPos - closest).manhattanLength() <= 8.0 / std::max(0.01, m_scale)) return i;
            continue;
        }
        QPointF center = m.rect.center();
        QTransform t;
        t.translate(center.x(), center.y());
        t.rotate(-m.rotation);
        t.translate(-center.x(), -center.y());
        QPointF local = t.map(imgPos);
        if (m.rect.contains(local)) return i;
    }
    return -1;
}

int ImageCanvas::paintMarkerAt(const QPoint &pos) const {
    QPointF imgPos = (QPointF(pos) - m_topLeft) / m_scale;
    for (int i = m_paintMarkers.size() - 1; i >= 0; --i)
        if (m_paintMarkers[i].contains(imgPos)) return i;
    return -1;
}

int ImageCanvas::imageLayerMarkerAt(const QPoint &pos) const {
    QPointF imgPos = (QPointF(pos) - m_topLeft) / m_scale;
    for (int i = m_imageLayerMarkers.size() - 1; i >= 0; --i)
        if (m_imageLayerMarkers[i].contains(imgPos)) return i;
    return -1;
}

QPointF ImageCanvas::shapeRotateHandlePos(const ShapeMarker &m) const {
    QPointF anchor = m.rect.center();
    QPointF handleImg(anchor.x(), m.rect.top() - 28.0 / std::max(0.01, m_scale));
    QTransform t;
    t.translate(anchor.x(), anchor.y());
    t.rotate(m.rotation);
    t.translate(-anchor.x(), -anchor.y());
    return m_topLeft + t.map(handleImg) * m_scale;
}

QPointF ImageCanvas::shapeCornerLocal(const ShapeMarker &m, Handle corner) const {
    switch (corner) {
    case Handle::TopLeft:     return m.rect.topLeft();
    case Handle::TopRight:    return QPointF(m.rect.right(), m.rect.top());
    case Handle::BottomLeft:  return QPointF(m.rect.left(), m.rect.bottom());
    case Handle::BottomRight: return m.rect.bottomRight();
    default:                  return m.rect.topLeft();
    }
}

QPointF ImageCanvas::shapeCornerScreenPos(const ShapeMarker &m, Handle corner) const {
    QPointF anchor = m.rect.center();
    QPointF local = shapeCornerLocal(m, corner);
    QTransform t;
    t.translate(anchor.x(), anchor.y());
    t.rotate(m.rotation);
    t.translate(-anchor.x(), -anchor.y());
    return m_topLeft + t.map(local) * m_scale;
}

ImageCanvas::Handle ImageCanvas::shapeCornerHandleAt(const QPoint &pos) const {
    if (m_activeShapeIndex < 0 || m_activeShapeIndex >= m_shapeMarkers.size()) return Handle::None;
    const ShapeMarker &m = m_shapeMarkers[m_activeShapeIndex];
    if (m.type == ShapeType::Line) return Handle::None;
    const double t = 10.0;
    for (Handle h : {Handle::TopLeft, Handle::TopRight, Handle::BottomLeft, Handle::BottomRight}) {
        if ((QPointF(pos) - shapeCornerScreenPos(m, h)).manhattanLength() <= t) return h;
    }
    return Handle::None;
}

QPointF ImageCanvas::shapeEndpointScreenPos(const ShapeMarker &m, bool first) const {
    return m_topLeft + (first ? m.p1 : m.p2) * m_scale;
}

int ImageCanvas::shapeEndpointAt(const QPoint &pos) const {
    if (m_activeShapeIndex < 0 || m_activeShapeIndex >= m_shapeMarkers.size()) return -1;
    const ShapeMarker &m = m_shapeMarkers[m_activeShapeIndex];
    if (m.type != ShapeType::Line) return -1;
    const double t = 10.0;
    if ((QPointF(pos) - shapeEndpointScreenPos(m, true)).manhattanLength() <= t) return 0;
    if ((QPointF(pos) - shapeEndpointScreenPos(m, false)).manhattanLength() <= t) return 1;
    return -1;
}

QRectF ImageCanvas::shapeGroupBounds() const {
    QRectF bounds;
    bool first = true;
    for (int i : m_selectedShapeIndices) {
        if (i < 0 || i >= m_shapeMarkers.size()) continue;
        const ShapeMarker &m = m_shapeMarkers[i];
        QRectF r;
        if (m.type == ShapeType::Line) {
            r = QRectF(m.p1, m.p2).normalized();
        } else {
            QPointF anchor = m.rect.center();
            QTransform t;
            t.translate(anchor.x(), anchor.y());
            t.rotate(m.rotation);
            t.translate(-anchor.x(), -anchor.y());
            QPolygonF poly;
            for (const QPointF &corner : {m.rect.topLeft(), QPointF(m.rect.right(), m.rect.top()),
                                          m.rect.bottomRight(), QPointF(m.rect.left(), m.rect.bottom())})
                poly << t.map(corner);
            r = poly.boundingRect();
        }
        bounds = first ? r : bounds.united(r);
        first = false;
    }
    return bounds;
}

ImageCanvas::Handle ImageCanvas::shapeGroupCornerHandleAt(const QPoint &pos) const {
    QRectF gb = shapeGroupBounds();
    if (gb.isEmpty()) return Handle::None;
    const double t = 10.0;
    const QPointF tl = m_topLeft + gb.topLeft() * m_scale;
    const QPointF tr = m_topLeft + QPointF(gb.right(), gb.top()) * m_scale;
    const QPointF bl = m_topLeft + QPointF(gb.left(), gb.bottom()) * m_scale;
    const QPointF br = m_topLeft + gb.bottomRight() * m_scale;
    if ((QPointF(pos) - tl).manhattanLength() <= t) return Handle::TopLeft;
    if ((QPointF(pos) - tr).manhattanLength() <= t) return Handle::TopRight;
    if ((QPointF(pos) - bl).manhattanLength() <= t) return Handle::BottomLeft;
    if ((QPointF(pos) - br).manhattanLength() <= t) return Handle::BottomRight;
    return Handle::None;
}

// Photoshop-style smart-guide snapping for Ctrl+drag: snaps `pos` (the box's
// top-left at the candidate drop point) so its left/center/right edges align
// with the canvas's edges/center or another text box's edges/center, within
// a screen-space tolerance. Populates m_activeGuideXs/Ys with the matched
// guide positions (image px) for paintEvent to draw while dragging.
QPointF ImageCanvas::snapTextPosition(const QPointF &pos, int index) {
    m_activeGuideXs.clear();
    m_activeGuideYs.clear();
    if (index < 0 || index >= m_textMarkers.size() || m_img.isNull()) return pos;

    const QSizeF size = m_textMarkers[index].rect.size();
    const double W = m_img.width(), H = m_img.height();
    const double tol = 8.0 / std::max(0.01, m_scale); // screen px -> image px

    QVector<double> xGuides = {0.0, W, W / 2.0};
    QVector<double> yGuides = {0.0, H, H / 2.0};
    for (int i = 0; i < m_textMarkers.size(); ++i) {
        if (i == index) continue;
        const QRectF &r = m_textMarkers[i].rect;
        xGuides << r.left() << r.right() << r.center().x();
        yGuides << r.top() << r.bottom() << r.center().y();
    }

    // For each axis independently, find the guide value closest to any of
    // the box's three candidate edges (left/center/right, or top/center/
    // bottom) at `pos`, within `tol`, and snap to it.
    QPointF result = pos;
    {
        double bestDist = tol, bestOffset = 0.0, bestGuide = 0.0;
        bool found = false;
        for (double g : xGuides) {
            for (double cand : {pos.x(), pos.x() + size.width() / 2.0, pos.x() + size.width()}) {
                double d = std::abs(cand - g);
                if (d < bestDist) { bestDist = d; bestOffset = g - cand; bestGuide = g; found = true; }
            }
        }
        if (found) {
            result.setX(pos.x() + bestOffset);
            m_activeGuideXs.append(bestGuide);
        }
    }
    {
        double bestDist = tol, bestOffset = 0.0, bestGuide = 0.0;
        bool found = false;
        for (double g : yGuides) {
            for (double cand : {pos.y(), pos.y() + size.height() / 2.0, pos.y() + size.height()}) {
                double d = std::abs(cand - g);
                if (d < bestDist) { bestDist = d; bestOffset = g - cand; bestGuide = g; found = true; }
            }
        }
        if (found) {
            result.setY(pos.y() + bestOffset);
            m_activeGuideYs.append(bestGuide);
        }
    }
    return result;
}

void ImageCanvas::setEraseMode(bool on) {
    m_eraseMode = on;
    if (!on) m_eraseDragging = false;
    update();
}

void ImageCanvas::setRemoveObjectMode(bool on) {
    m_removeObjectMode = on;
    if (!on) m_removeObjectDragging = false;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setRemoveObjectBusy(bool busy) {
    m_removeObjectBusy = busy;
    if (busy) m_removeObjectDragging = false;
    update();
}

void ImageCanvas::setRemovalMarkers(const QVector<RemovalMarker> &markers) {
    m_removalMarkers = markers;
    if (m_activeRemovalIndex >= m_removalMarkers.size()) m_activeRemovalIndex = -1;
    update();
}

void ImageCanvas::setActiveRemovalIndex(int index) {
    m_activeRemovalIndex = index;
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

void ImageCanvas::setMaskForceErase(bool on) {
    m_maskForceErase = on;
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
        emit cropSelected(selectionInImage(), m_cropAngle);
    }
}

void ImageCanvas::clearSelection() {
    m_drag = Drag::None;
    m_p0 = m_p1 = QPoint();
    m_cropAngle = 0.0;
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

void ImageCanvas::centerOnImagePoint(const QPointF &imagePt) {
    if (m_img.isNull()) return;
    m_fit = false;
    m_topLeft = QPointF(width() / 2.0, height() / 2.0) - imagePt * m_scale;
    clampPan();
    update();
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

QPointF ImageCanvas::cropLocalPoint(const QPoint &pos) const {
    QRect r = selectionRect();
    if (m_cropAngle == 0.0 || r.isEmpty()) return QPointF(pos);
    QPointF center = r.center();
    QTransform t;
    t.translate(center.x(), center.y());
    t.rotate(-m_cropAngle);
    t.translate(-center.x(), -center.y());
    return t.map(QPointF(pos));
}

bool ImageCanvas::cropRectContains(const QPoint &pos) const {
    return selectionRect().contains(cropLocalPoint(pos).toPoint());
}

ImageCanvas::Handle ImageCanvas::handleAt(const QPoint &pos) const {
    QRect r = selectionRect();
    if (r.isEmpty()) return Handle::None;
    QPointF local = cropLocalPoint(pos);
    const int t = 10; // grab tolerance in widget px
    auto near = [&](double a, double b) { return std::abs(a - b) <= t; };
    bool onLeft   = near(local.x(), r.left());
    bool onRight  = near(local.x(), r.right());
    bool onTop    = near(local.y(), r.top());
    bool onBottom = near(local.y(), r.bottom());
    // Only count edge hits when the other axis is within the rect span (± tol).
    bool inX = local.x() >= r.left() - t && local.x() <= r.right() + t;
    bool inY = local.y() >= r.top() - t && local.y() <= r.bottom() + t;
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

// Photoshop-style rotate ring: only the four corners rotate (not the edge
// midpoints), and only in the ring just beyond the corner's resize-handle
// tolerance, so a normal corner-resize drag still takes priority.
bool ImageCanvas::cropInRotateZone(const QPoint &pos) const {
    QRect r = selectionRect();
    if (r.isEmpty()) return false;
    QPointF local = cropLocalPoint(pos);
    const double innerT = 10.0, outerT = 26.0;
    const QPointF corners[4] = { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() };
    for (const QPointF &c : corners) {
        double d = std::hypot(local.x() - c.x(), local.y() - c.y());
        if (d > innerT && d <= outerT) return true;
    }
    return false;
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
    if (m_showCheckerboard) {
        static const int kTile = 10;
        static QPixmap checker;
        if (checker.isNull()) {
            checker = QPixmap(kTile * 2, kTile * 2);
            QPainter cp(&checker);
            cp.fillRect(checker.rect(), QColor(90, 90, 90));
            cp.fillRect(0, 0, kTile, kTile, QColor(120, 120, 120));
            cp.fillRect(kTile, kTile, kTile, kTile, QColor(120, 120, 120));
        }
        p.drawTiledPixmap(tr, checker);
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1.0);
    p.drawImage(tr, m_img);

    if (m_cropMode && (m_drag != Drag::None || !selectionRect().isEmpty())) {
        QRect sel = selectionRect();
        if (!sel.isEmpty()) {
            p.save();
            QPointF center = sel.center();
            p.translate(center);
            p.rotate(m_cropAngle);
            p.translate(-center);
            // Darken everything outside sel but inside tr. tr is mapped into
            // sel's local (unrotated) frame so it lines up correctly once the
            // painter's own rotation above is applied.
            QTransform inv;
            inv.translate(center.x(), center.y());
            inv.rotate(-m_cropAngle);
            inv.translate(-center.x(), -center.y());
            QPainterPath outside;
            outside.addPolygon(inv.map(QPolygonF(QRectF(tr))));
            outside.closeSubpath();
            QPainterPath selPath;
            selPath.addRect(sel);
            outside = outside.subtracted(selPath);
            p.fillPath(outside, QColor(0, 0, 0, 120));
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
            p.restore();
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

    if (m_removeObjectMode && underMouse() && !m_removeObjectBusy) {
        double rad = m_brushRadius * m_scale;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(120, 200, 255, 220), 1));
        p.setBrush(QColor(90, 170, 255, 40));
        p.drawEllipse(QPointF(m_mousePos), rad, rad);
    }

    // Object-removal markers: outline every removal's bounding box, and
    // highlight whichever one is selected in the Layers panel — only while
    // the remove-object tool itself is active (mirrors every other tool's
    // marker convention, e.g. text/shape below), so the outline doesn't
    // linger on canvas (or leak into exports/screenshots) once the user has
    // moved on to a different tool.
    if (m_removeObjectMode && !m_removalMarkers.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_removalMarkers.size(); ++i) {
            if (i == m_activeRemovalIndex) continue; // drawn highlighted below
            const RemovalMarker &m = m_removalMarkers[i];
            QRectF r(m_topLeft + m.rect.topLeft() * m_scale, m.rect.size() * m_scale);
            p.setPen(QPen(QColor(200, 200, 200, 160), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
        if (m_activeRemovalIndex >= 0 && m_activeRemovalIndex < m_removalMarkers.size()) {
            const RemovalMarker &m = m_removalMarkers[m_activeRemovalIndex];
            QRectF r(m_topLeft + m.rect.topLeft() * m_scale, m.rect.size() * m_scale);
            p.setPen(QPen(QColor(120, 200, 255), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
    }

    // Text tool: outline every placed text, highlight + rotate handle on the
    // active one.
    if (m_textMode) {
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_textMarkers.size(); ++i) {
            const TextMarker &m = m_textMarkers[i];
            bool active = (i == m_activeTextIndex);
            QPointF anchor = m.rect.topLeft();
            // Rotate the box's corners in local (unrotated) image space about
            // the anchor, then map each to screen space once — mirrors
            // textCornerScreenPos/textRotateHandlePos. Rotating the QPainter
            // itself around an already screen-mapped rect double-applies
            // m_topLeft and shifts the box away from the actual text.
            QTransform t;
            t.translate(anchor.x(), anchor.y());
            t.rotate(m.rotation);
            t.translate(-anchor.x(), -anchor.y());
            QPolygonF poly;
            for (const QPointF &corner : {m.rect.topLeft(), QPointF(m.rect.right(), m.rect.top()),
                                          m.rect.bottomRight(), QPointF(m.rect.left(), m.rect.bottom())})
                poly << (m_topLeft + t.map(corner) * m_scale);
            p.setPen(QPen(active ? QColor(120, 200, 255) : QColor(200, 200, 200, 160),
                         active ? 2 : 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(poly);
        }
        if (m_activeTextIndex >= 0 && m_activeTextIndex < m_textMarkers.size()) {
            const TextMarker &am = m_textMarkers[m_activeTextIndex];
            QPointF hp = textRotateHandlePos(am);
            p.setPen(QPen(QColor(120, 200, 255), 2));
            p.setBrush(QColor(30, 30, 30));
            p.drawEllipse(hp, 6, 6);
            p.setPen(QPen(QColor(120, 200, 255), 1.5));
            p.setBrush(QColor(120, 200, 255));
            for (Handle h : {Handle::TopRight, Handle::BottomLeft, Handle::BottomRight})
                p.drawRect(QRectF(textCornerScreenPos(am, h) - QPointF(4, 4), QSizeF(8, 8)));
        }
        // Smart-guide lines matched during a Ctrl+drag (see snapTextPosition),
        // shown only while actively dragging.
        if (!m_activeGuideXs.isEmpty() || !m_activeGuideYs.isEmpty()) {
            p.setPen(QPen(QColor(255, 60, 200), 1, Qt::DashLine));
            for (double gx : m_activeGuideXs) {
                double sx = m_topLeft.x() + gx * m_scale;
                p.drawLine(QPointF(sx, 0), QPointF(sx, height()));
            }
            for (double gy : m_activeGuideYs) {
                double sy = m_topLeft.y() + gy * m_scale;
                p.drawLine(QPointF(0, sy), QPointF(width(), sy));
            }
        }
    }

    // Shape tool: outline every placed shape, highlight + resize/rotate
    // handles (or endpoint handles for a Line) on the active one.
    if (m_shapeMode) {
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_shapeMarkers.size(); ++i) {
            const ShapeMarker &m = m_shapeMarkers[i];
            bool selected = m_selectedShapeIndices.contains(i) || i == m_activeShapeIndex;
            p.setPen(QPen(selected ? QColor(120, 200, 255) : QColor(200, 200, 200, 160),
                         selected ? 2 : 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            if (m.type == ShapeType::Line) {
                p.drawLine(m_topLeft + m.p1 * m_scale, m_topLeft + m.p2 * m_scale);
                continue;
            }
            QPointF anchor = m.rect.center();
            QTransform t;
            t.translate(anchor.x(), anchor.y());
            t.rotate(m.rotation);
            t.translate(-anchor.x(), -anchor.y());
            QPolygonF poly;
            for (const QPointF &corner : {m.rect.topLeft(), QPointF(m.rect.right(), m.rect.top()),
                                          m.rect.bottomRight(), QPointF(m.rect.left(), m.rect.bottom())})
                poly << (m_topLeft + t.map(corner) * m_scale);
            p.drawPolygon(poly);
        }
        if (m_activeShapeIndex >= 0 && m_activeShapeIndex < m_shapeMarkers.size()) {
            const ShapeMarker &am = m_shapeMarkers[m_activeShapeIndex];
            if (am.type == ShapeType::Line) {
                p.setPen(QPen(QColor(120, 200, 255), 1.5));
                p.setBrush(QColor(120, 200, 255));
                p.drawRect(QRectF(shapeEndpointScreenPos(am, true) - QPointF(4, 4), QSizeF(8, 8)));
                p.drawRect(QRectF(shapeEndpointScreenPos(am, false) - QPointF(4, 4), QSizeF(8, 8)));
            } else {
                QPointF hp = shapeRotateHandlePos(am);
                p.setPen(QPen(QColor(120, 200, 255), 2));
                p.setBrush(QColor(30, 30, 30));
                p.drawEllipse(hp, 6, 6);
                p.setPen(QPen(QColor(120, 200, 255), 1.5));
                p.setBrush(QColor(120, 200, 255));
                for (Handle h : {Handle::TopLeft, Handle::TopRight, Handle::BottomLeft, Handle::BottomRight})
                    p.drawRect(QRectF(shapeCornerScreenPos(am, h) - QPointF(4, 4), QSizeF(8, 8)));
            }
        }
        if (m_shapeDrag == ShapeDrag::Creating) {
            p.setPen(QPen(QColor(120, 200, 255), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRect(m_shapeCreateP0, m_shapeCreateP1).normalized());
        }
        // Multi-selection: combined bounding box with its own corner
        // handles (orange, distinct from a single shape's blue handles),
        // for a group resize dragging all selected shapes together.
        if (m_selectedShapeIndices.size() > 1) {
            QRectF gb = shapeGroupBounds();
            if (!gb.isEmpty()) {
                QRectF screenBounds(m_topLeft + gb.topLeft() * m_scale, gb.size() * m_scale);
                p.setPen(QPen(QColor(255, 190, 60), 1.5, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(screenBounds);
                p.setPen(QPen(QColor(255, 190, 60), 1.5));
                p.setBrush(QColor(255, 190, 60));
                for (const QPointF &corner : {screenBounds.topLeft(), screenBounds.topRight(),
                                              screenBounds.bottomLeft(), screenBounds.bottomRight()})
                    p.drawRect(QRectF(corner - QPointF(4, 4), QSizeF(8, 8)));
            }
        }
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
        } else if (m.type == MaskType::Brush || m.type == MaskType::Paint) {
            // Brush (mask selection) shows its painted coverage; Paint
            // (direct color fill) paints straight into the composited
            // image, so it has no overlay to preview. Both share the same
            // brush cursor circle at the current radius.
            if (m.type == MaskType::Brush && !m_maskOverlay.isNull())
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
        // Image/None/Text/Shape/TextBox: no local-mask gizmo of their own to
        // draw here (Shape/TextBox use their own marker-based gizmo drawn
        // elsewhere in this function; Image/None have no localized region
        // to visualize).
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

    if (m_showRulers && !m_img.isNull()) {
        drawGuides(p);
        if (m_guideDrag == GuideDrag::NewH) {
            p.save();
            p.setPen(QPen(QColor(0, 200, 255), 1, Qt::DashLine));
            p.drawLine(QPointF(0, m_guideDragPos.y()), QPointF(width(), m_guideDragPos.y()));
            p.restore();
        } else if (m_guideDrag == GuideDrag::NewV) {
            p.save();
            p.setPen(QPen(QColor(0, 200, 255), 1, Qt::DashLine));
            p.drawLine(QPointF(m_guideDragPos.x(), 0), QPointF(m_guideDragPos.x(), height()));
            p.restore();
        }
        drawRulers(p);
    }
}

void ImageCanvas::setGuides(const QVector<double> &horizontal, const QVector<double> &vertical) {
    m_guidesH = horizontal;
    m_guidesV = vertical;
    update();
}

// Which horizontal guide (m_guidesH index) is under pos, or -1. Hit-tested
// against the whole viewport, not just the image, so a guide dragged past
// the image edge can still be grabbed.
int ImageCanvas::guideHAt(const QPoint &pos) const {
    const int kTol = 4;
    for (int i = 0; i < m_guidesH.size(); ++i) {
        double wy = m_topLeft.y() + m_guidesH[i] * m_img.height() * m_scale;
        if (std::abs(pos.y() - wy) <= kTol) return i;
    }
    return -1;
}

int ImageCanvas::guideVAt(const QPoint &pos) const {
    const int kTol = 4;
    for (int i = 0; i < m_guidesV.size(); ++i) {
        double wx = m_topLeft.x() + m_guidesV[i] * m_img.width() * m_scale;
        if (std::abs(pos.x() - wx) <= kTol) return i;
    }
    return -1;
}

// Dragged-out guide lines: full-viewport lines in Photoshop's cyan, drawn
// over the image but under the ruler bands (drawRulers paints last).
void ImageCanvas::drawGuides(QPainter &p) {
    if (m_guidesH.isEmpty() && m_guidesV.isEmpty()) return;
    p.save();
    QPen pen(QColor(0, 200, 255));
    pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    for (double h : m_guidesH) {
        double wy = m_topLeft.y() + h * m_img.height() * m_scale;
        p.drawLine(QPointF(0, wy), QPointF(width(), wy));
    }
    for (double v : m_guidesV) {
        double wx = m_topLeft.x() + v * m_img.width() * m_scale;
        p.drawLine(QPointF(wx, 0), QPointF(wx, height()));
    }
    p.restore();
}

// Photoshop-style rulers along the top and left edges, drawn as an overlay
// (the canvas has no QScrollArea inset to push content into). Ticks are
// spaced at a "nice" image-pixel interval (1/2/5 * 10^n) chosen so labelled
// major ticks stay at least ~50 widget px apart at the current zoom.
void ImageCanvas::drawRulers(QPainter &p) {
    const int kThickness = 20;
    const QColor bg(50, 50, 50);
    const QColor tick(160, 160, 160);
    const QColor text(210, 210, 210);

    // Choose a "nice" step (in image px) for major ticks.
    double rawStep = 50.0 / m_scale; // image px per ~50 widget px
    static const double bases[] = {1, 2, 5};
    double step = 1.0;
    double mag = 1.0;
    while (mag * bases[2] < rawStep) mag *= 10.0;
    for (double b : bases) {
        if (mag * b >= rawStep) { step = mag * b; break; }
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    // Subpixel (LCD) text antialiasing assumes horizontal RGB striping and
    // produces colour-fringed, garbled glyphs once rotated 90° for the left
    // ruler's labels, so force plain grayscale antialiasing for ruler text.
    QFont rulerFont = p.font();
    rulerFont.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    rulerFont.setHintingPreference(QFont::PreferNoHinting);
    p.setFont(rulerFont);

    // Top ruler.
    p.fillRect(QRect(0, 0, width(), kThickness), bg);
    double firstX = std::floor((-m_topLeft.x() / m_scale) / step) * step;
    for (double v = firstX; ; v += step) {
        double wx = m_topLeft.x() + v * m_scale;
        if (wx > width()) break;
        if (wx >= 0) {
            p.setPen(tick);
            p.drawLine(QPointF(wx, kThickness - 8), QPointF(wx, kThickness));
            p.setPen(text);
            p.drawText(QRectF(wx + 2, 0, 60, kThickness - 8), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(qRound(v)));
        }
        // Minor tick at the midpoint.
        double wxMid = m_topLeft.x() + (v + step / 2.0) * m_scale;
        if (wxMid >= 0 && wxMid <= width()) {
            p.setPen(tick);
            p.drawLine(QPointF(wxMid, kThickness - 4), QPointF(wxMid, kThickness));
        }
    }

    // Left ruler.
    p.fillRect(QRect(0, 0, kThickness, height()), bg);
    double firstY = std::floor((-m_topLeft.y() / m_scale) / step) * step;
    for (double v = firstY; ; v += step) {
        double wy = m_topLeft.y() + v * m_scale;
        if (wy > height()) break;
        if (wy >= 0) {
            p.setPen(tick);
            p.drawLine(QPointF(kThickness - 8, wy), QPointF(kThickness, wy));
            p.setPen(text);
            p.save();
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.translate(kThickness - 2, qRound(wy));
            p.rotate(-90);
            p.drawText(QRectF(-22, -(kThickness - 4), 44, kThickness - 4),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(qRound(v)));
            p.restore();
        }
        double wyMid = m_topLeft.y() + (v + step / 2.0) * m_scale;
        if (wyMid >= 0 && wyMid <= height()) {
            p.setPen(tick);
            p.drawLine(QPointF(kThickness - 4, wyMid), QPointF(kThickness, wyMid));
        }
    }

    // Current mouse position indicator (small triangles) and corner square.
    if (rect().contains(m_mousePos)) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 180, 0));
        p.drawEllipse(QPointF(m_mousePos.x(), kThickness / 2.0), 2, 2);
        p.drawEllipse(QPointF(kThickness / 2.0, m_mousePos.y()), 2, 2);
    }
    p.fillRect(QRect(0, 0, kThickness, kThickness), bg);

    p.restore();
}

// ---- Mouse -----------------------------------------------------------------

void ImageCanvas::mousePressEvent(QMouseEvent *ev) {
    if (m_img.isNull()) return;

    // Ruler guides: take priority over every tool while rulers are shown, so
    // guides stay reachable regardless of the active tool (Photoshop-style).
    if (m_showRulers && ev->button() == Qt::LeftButton) {
        const int kThickness = 20;
        int hv = guideVAt(ev->pos());
        int hh = guideHAt(ev->pos());
        if (hv >= 0 && ev->pos().y() >= kThickness) {
            m_guideDrag = GuideDrag::MoveV;
            m_guideDragIndex = hv;
            setCursor(Qt::SizeHorCursor);
            update();
            return;
        }
        if (hh >= 0 && ev->pos().x() >= kThickness) {
            m_guideDrag = GuideDrag::MoveH;
            m_guideDragIndex = hh;
            setCursor(Qt::SizeVerCursor);
            update();
            return;
        }
        if (ev->pos().y() < kThickness && ev->pos().x() >= kThickness) {
            // Pressed on the top ruler: start dragging out a new horizontal guide.
            m_guideDrag = GuideDrag::NewH;
            m_guideDragPos = ev->pos();
            setCursor(Qt::SizeVerCursor);
            update();
            return;
        }
        if (ev->pos().x() < kThickness && ev->pos().y() >= kThickness) {
            // Pressed on the left ruler: start dragging out a new vertical guide.
            m_guideDrag = GuideDrag::NewV;
            m_guideDragPos = ev->pos();
            setCursor(Qt::SizeHorCursor);
            update();
            return;
        }
    }

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

    // Paint bucket: single click, no drag.
    if (m_bucketMode && ev->button() == Qt::LeftButton) {
        QPoint ip = imagePointAt(ev->pos());
        if (ip.x() >= 0) emit bucketFillRequested(normPointAt(ev->pos()));
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

    // Text tool: click an existing text's rotate handle to rotate, its body
    // to move, or empty canvas to place a new one.
    if (m_textMode && ev->button() == Qt::LeftButton) {
        if (m_textEditor && m_textEditor->isVisible()) commitTextEditor();
        if (m_activeTextIndex >= 0 && m_activeTextIndex < m_textMarkers.size()) {
            const TextMarker &am = m_textMarkers[m_activeTextIndex];
            QPointF hp = textRotateHandlePos(am);
            if ((QPointF(ev->pos()) - hp).manhattanLength() <= 10) {
                m_textDrag = TextDrag::Rotating;
                m_textDragStartMouse = ev->pos();
                m_textRotateStartAngle = am.rotation;
                return;
            }
            Handle corner = textCornerHandleAt(ev->pos());
            if (corner != Handle::None) {
                QPointF anchor = am.rect.topLeft();
                QPointF localCorner = textCornerLocal(am, corner);
                m_textDrag = TextDrag::Resizing;
                m_textResizeCorner = corner;
                m_textResizeStartDist = std::max(
                    1.0, std::hypot(localCorner.x() - anchor.x(), localCorner.y() - anchor.y()));
                emit textResizeStarted(m_activeTextIndex);
                return;
            }
        }
        int hit = textMarkerAt(ev->pos());
        if (hit >= 0) {
            m_activeTextIndex = hit;
            m_textDrag = TextDrag::Moving;
            m_textDragStartMouse = ev->pos();
            m_textDragStartImgPos = m_textMarkers[hit].rect.topLeft();
            emit textSelected(hit);
            update();
            return;
        }
        QPoint ip = imagePointAt(ev->pos());
        if (ip.x() >= 0) {
            m_activeTextIndex = -1;
            emit textDeselected();
            emit textPlaceRequested(ip);
        }
        return;
    }

    // Shape tool: click an existing shape's rotate handle to rotate, a
    // corner/endpoint to resize, its body to move, or empty canvas to
    // drag-create a new one.
    if (m_shapeMode && ev->button() == Qt::LeftButton) {
        // A multi-selection's combined bounding-box handles take priority
        // over any individual active-shape handle underneath.
        if (m_selectedShapeIndices.size() > 1) {
            Handle gcorner = shapeGroupCornerHandleAt(ev->pos());
            if (gcorner != Handle::None) {
                m_shapeDrag = ShapeDrag::ResizingGroup;
                m_shapeGroupResizeCorner = gcorner;
                m_shapeDragStartMouse = ev->pos();
                m_shapeGroupResizeStartBounds = shapeGroupBounds();
                m_shapeGroupIndices = m_selectedShapeIndices.values();
                emit shapeGroupResizeStarted(m_shapeGroupIndices);
                return;
            }
        }
        if (m_activeShapeIndex >= 0 && m_activeShapeIndex < m_shapeMarkers.size()) {
            const ShapeMarker &am = m_shapeMarkers[m_activeShapeIndex];
            if (am.type == ShapeType::Line) {
                int ep = shapeEndpointAt(ev->pos());
                if (ep >= 0) {
                    m_shapeDrag = ShapeDrag::EndpointDrag;
                    m_shapeEndpointDragging = ep;
                    m_shapeDragStartMouse = ev->pos();
                    m_shapeDragStartP1 = am.p1;
                    m_shapeDragStartP2 = am.p2;
                    return;
                }
            } else {
                QPointF hp = shapeRotateHandlePos(am);
                if ((QPointF(ev->pos()) - hp).manhattanLength() <= 10) {
                    m_shapeDrag = ShapeDrag::Rotating;
                    m_shapeDragStartMouse = ev->pos();
                    m_shapeRotateStartAngle = am.rotation;
                    return;
                }
                Handle corner = shapeCornerHandleAt(ev->pos());
                if (corner != Handle::None) {
                    m_shapeDrag = ShapeDrag::Resizing;
                    m_shapeResizeCorner = corner;
                    m_shapeDragStartMouse = ev->pos();
                    m_shapeDragStartTopLeft = am.rect.topLeft();
                    m_shapeResizeStartSize = am.rect.size();
                    return;
                }
            }
        }
        int hit = shapeMarkerAt(ev->pos());
        if (hit >= 0) {
            if (ev->modifiers() & Qt::ControlModifier) {
                // Ambiguous until release: resolved to a duplicate-drag once
                // the mouse moves past a threshold (mouseMoveEvent), or to a
                // plain toggle-select if released without much movement
                // (mouseReleaseEvent). If the hit shape is already part of a
                // multi-selection, the whole group is duplicated together.
                m_shapeCtrlPending = true;
                m_shapeCtrlPendingHit = hit;
                m_shapeCtrlPendingGroup = m_selectedShapeIndices.contains(hit) &&
                                          m_selectedShapeIndices.size() > 1;
                m_shapeDragStartMouse = ev->pos();
                return;
            }
            if (!(m_selectedShapeIndices.contains(hit) && m_selectedShapeIndices.size() > 1)) {
                // Not already part of a multi-selection: select it (the slot
                // may synchronously expand the selection to the shape's
                // persistent group via setSelectedShapeIndices — re-checked
                // below before deciding Moving vs MovingGroup).
                m_activeShapeIndex = hit;
                emit shapeSelected(hit);
            }
            if (m_selectedShapeIndices.contains(hit) && m_selectedShapeIndices.size() > 1) {
                // Either already a multi-selection, or shapeSelected just
                // expanded it to the shape's group: drag the whole group.
                m_shapeDrag = ShapeDrag::MovingGroup;
                m_shapeDragStartMouse = ev->pos();
                m_shapeGroupIndices = m_selectedShapeIndices.values();
                emit shapeGroupMoveStarted(m_shapeGroupIndices);
                update();
                return;
            }
            m_shapeDrag = ShapeDrag::Moving;
            m_shapeDragStartMouse = ev->pos();
            m_shapeDragStartTopLeft = m_shapeMarkers[hit].rect.topLeft();
            m_shapeDragStartP1 = m_shapeMarkers[hit].p1;
            m_shapeDragStartP2 = m_shapeMarkers[hit].p2;
            update();
            return;
        }
        m_activeShapeIndex = -1;
        emit shapeDeselected();
        m_shapeDrag = ShapeDrag::Creating;
        m_shapeCreateP0 = ev->pos();
        m_shapeCreateP1 = ev->pos();
        update();
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
        m_maskErasing = m_maskForceErase || ev->modifiers().testFlag(Qt::AltModifier);
        if (m_maskKind == MaskType::Radial)
            emit maskRadialDragged(n, 0.0);
        else if (m_maskKind == MaskType::Linear)
            emit maskLinearDragged(n, n);
        else {
            m_lastBrushNorm = n;
            emit maskBrushPoint(n, m_maskErasing, true);
        }
        update();
        return;
    }

    // Erase brush: active whenever any layer is selected, regardless of type.
    if (m_eraseMode && m_hasActiveMask && ev->button() == Qt::LeftButton) {
        QPointF n = normPointAt(ev->pos());
        m_eraseDragging = true;
        m_lastEraseNorm = n;
        m_mousePos = ev->pos();
        emit eraseAt(n);
        update();
        return;
    }

    // Remove-object brush: paints a stroke over an unwanted object; on
    // release, RetouchTab runs the content-aware fill.
    if (m_removeObjectMode && !m_removeObjectBusy && ev->button() == Qt::LeftButton) {
        QPointF n = normPointAt(ev->pos());
        m_removeObjectDragging = true;
        m_lastRemoveObjectNorm = n;
        m_mousePos = ev->pos();
        emit removeObjectAt(n);
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
        } else if (cropInRotateZone(ev->pos())) {
            m_drag = Drag::Rotating;
            m_rectAtDragStart = selectionRect();
            m_cropDragStartMouse = ev->pos();
            m_cropRotateStartAngle = m_cropAngle;
        } else if (cropRectContains(ev->pos())) {
            m_drag = Drag::Moving;
            m_moveStart = ev->pos();
            m_rectAtMoveStart = selectionRect();
            setCursor(Qt::ClosedHandCursor);
        } else {
            m_drag = Drag::Creating;
            m_p0 = m_p1 = ev->pos();
            m_cropAngle = 0.0;
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

    // Click-to-select fallback: nothing above (crop/heal/mask/erase/
    // shape/text/etc.) claimed this click, so hit-test every other
    // selectable object — topmost type first — and let RetouchTab select
    // its layer and switch to its tool. Shapes/text already have precise
    // rotated hit-testing via shapeMarkerAt/textMarkerAt; Paint layers and
    // image layers use simple bounding-box markers (see setPaintMarkers/
    // setImageLayerMarkers).
    if (ev->button() == Qt::LeftButton && !m_spaceDown) {
        int hit;
        if ((hit = shapeMarkerAt(ev->pos())) >= 0) { emit objectClicked(MaskType::Shape, hit); return; }
        if ((hit = textMarkerAt(ev->pos())) >= 0) { emit objectClicked(MaskType::TextBox, hit); return; }
        if ((hit = paintMarkerAt(ev->pos())) >= 0) { emit objectClicked(MaskType::Paint, hit); return; }
        if ((hit = imageLayerMarkerAt(ev->pos())) >= 0) { emit objectClicked(MaskType::Background, hit); return; }
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
    if (m_guideDrag != GuideDrag::None) {
        m_mousePos = ev->pos();
        if (m_guideDrag == GuideDrag::MoveH && m_img.height() > 0) {
            m_guidesH[m_guideDragIndex] = (ev->pos().y() - m_topLeft.y()) / m_scale / m_img.height();
        } else if (m_guideDrag == GuideDrag::MoveV && m_img.width() > 0) {
            m_guidesV[m_guideDragIndex] = (ev->pos().x() - m_topLeft.x()) / m_scale / m_img.width();
        } else {
            m_guideDragPos = ev->pos();
        }
        update();
        return;
    }
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
                    m_maskErasing = m_maskForceErase || ev->modifiers().testFlag(Qt::AltModifier);
                    emit maskBrushPoint(n, m_maskErasing, false);
                }
            }
        } else if (m_maskKind == MaskType::Brush) {
            m_maskErasing = m_maskForceErase || ev->modifiers().testFlag(Qt::AltModifier);
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
    if (m_removeObjectMode) {
        m_mousePos = ev->pos();
        if (m_removeObjectDragging) {
            QPointF n = normPointAt(ev->pos());
            double dx = n.x() - m_lastRemoveObjectNorm.x();
            double dy = n.y() - m_lastRemoveObjectNorm.y();
            if (dx * dx + dy * dy > 0.004 * 0.004) { // throttle stroke samples
                m_lastRemoveObjectNorm = n;
                emit removeObjectAt(n);
            }
        }
        update();
        return;
    }
    if (m_textMode && m_textDrag != TextDrag::None &&
        m_activeTextIndex >= 0 && m_activeTextIndex < m_textMarkers.size()) {
        if (m_textDrag == TextDrag::Moving) {
            QPointF delta = (QPointF(ev->pos()) - QPointF(m_textDragStartMouse)) / m_scale;
            if (ev->modifiers() & Qt::ShiftModifier) {
                // Axis-lock to whichever direction has moved further from
                // the drag start (Photoshop Move-tool convention).
                if (std::abs(delta.x()) >= std::abs(delta.y())) delta.setY(0);
                else delta.setX(0);
            }
            QPointF newPos = m_textDragStartImgPos + delta;
            if (ev->modifiers() & Qt::ControlModifier)
                newPos = snapTextPosition(newPos, m_activeTextIndex);
            else {
                m_activeGuideXs.clear();
                m_activeGuideYs.clear();
            }
            emit textMoved(m_activeTextIndex, newPos);
        } else if (m_textDrag == TextDrag::Rotating) {
            const TextMarker &am = m_textMarkers[m_activeTextIndex];
            QPointF anchor = m_topLeft + am.rect.topLeft() * m_scale;
            double a0 = std::atan2(m_textDragStartMouse.y() - anchor.y(),
                                   m_textDragStartMouse.x() - anchor.x());
            double a1 = std::atan2(ev->pos().y() - anchor.y(), ev->pos().x() - anchor.x());
            double deltaDeg = (a1 - a0) * 180.0 / M_PI;
            emit textRotated(m_activeTextIndex, m_textRotateStartAngle + deltaDeg);
        } else { // Resizing
            const TextMarker &am = m_textMarkers[m_activeTextIndex];
            QPointF anchor = am.rect.topLeft();
            // Unrotate the mouse position into the box's local (unrotated)
            // frame so the distance-from-anchor comparison is meaningful
            // regardless of the box's current rotation.
            QTransform t;
            t.translate(anchor.x(), anchor.y());
            t.rotate(-am.rotation);
            t.translate(-anchor.x(), -anchor.y());
            QPointF localMouse = t.map((QPointF(ev->pos()) - m_topLeft) / m_scale);
            double dist = std::hypot(localMouse.x() - anchor.x(), localMouse.y() - anchor.y());
            double ratio = std::clamp(dist / m_textResizeStartDist, 0.1, 20.0);
            emit textResized(m_activeTextIndex, ratio);
        }
        return;
    }
    if (m_shapeMode && m_shapeCtrlPending) {
        double dist = (QPointF(ev->pos()) - QPointF(m_shapeDragStartMouse)).manhattanLength();
        if (dist > 4.0) {
            m_shapeCtrlPending = false;
            if (m_shapeCtrlPendingGroup) {
                // The slot duplicates every selected shape and updates the
                // selection (via setSelectedShapeIndices/setActiveShapeIndex)
                // synchronously, so both reflect the new copies once emit
                // returns; the copies' start geometry is also captured there
                // (identical to the originals), so no separate "started"
                // signal is needed before the group drag continues.
                emit shapeGroupDuplicateRequested(m_selectedShapeIndices.values());
                m_shapeGroupIndices = m_selectedShapeIndices.values();
                m_shapeDrag = ShapeDrag::MovingGroup;
            } else {
                // Resolved to a single-shape duplicate-drag: the slot
                // duplicates the shape and calls setActiveShapeIndex()
                // synchronously, so m_activeShapeIndex points at the new
                // copy once emit returns.
                emit shapeDuplicateRequested(m_shapeCtrlPendingHit);
                int hit = m_activeShapeIndex;
                if (hit >= 0 && hit < m_shapeMarkers.size()) {
                    m_shapeDrag = ShapeDrag::Moving;
                    m_shapeDragStartTopLeft = m_shapeMarkers[hit].rect.topLeft();
                    m_shapeDragStartP1 = m_shapeMarkers[hit].p1;
                    m_shapeDragStartP2 = m_shapeMarkers[hit].p2;
                }
            }
            update();
        }
        return;
    }
    if (m_shapeMode && m_shapeDrag == ShapeDrag::Creating) {
        m_shapeCreateP1 = ev->pos();
        update();
        return;
    }
    if (m_shapeMode && m_shapeDrag == ShapeDrag::MovingGroup) {
        QPointF delta = (QPointF(ev->pos()) - QPointF(m_shapeDragStartMouse)) / m_scale;
        emit shapeGroupMoveRequested(m_shapeGroupIndices, delta);
        return;
    }
    if (m_shapeMode && m_shapeDrag == ShapeDrag::ResizingGroup) {
        QPointF mouseImg = (QPointF(ev->pos()) - m_topLeft) / m_scale;
        const QRectF &r = m_shapeGroupResizeStartBounds;
        QPointF fixedCorner;
        switch (m_shapeGroupResizeCorner) {
            case Handle::TopLeft:     fixedCorner = r.bottomRight(); break;
            case Handle::TopRight:    fixedCorner = r.bottomLeft(); break;
            case Handle::BottomLeft:  fixedCorner = r.topRight(); break;
            case Handle::BottomRight: fixedCorner = r.topLeft(); break;
            default:                  fixedCorner = r.topLeft(); break;
        }
        double newW = std::abs(mouseImg.x() - fixedCorner.x());
        double newH = std::abs(mouseImg.y() - fixedCorner.y());
        if ((ev->modifiers() & Qt::ShiftModifier) && r.width() > 0 && r.height() > 0) {
            double aspect = r.width() / r.height();
            if (newW > newH * aspect) newH = newW / aspect;
            else newW = newH * aspect;
        }
        double scaleX = r.width() > 1e-6 ? std::max(0.02, newW / r.width()) : 1.0;
        double scaleY = r.height() > 1e-6 ? std::max(0.02, newH / r.height()) : 1.0;
        emit shapeGroupResizeRequested(m_shapeGroupIndices, fixedCorner, scaleX, scaleY);
        return;
    }
    if (m_shapeMode && m_shapeDrag != ShapeDrag::None &&
        m_activeShapeIndex >= 0 && m_activeShapeIndex < m_shapeMarkers.size()) {
        const ShapeMarker &am = m_shapeMarkers[m_activeShapeIndex];
        if (m_shapeDrag == ShapeDrag::EndpointDrag) {
            QPointF imgPos = (QPointF(ev->pos()) - m_topLeft) / m_scale;
            QPointF newP1 = (m_shapeEndpointDragging == 0) ? imgPos : am.p1;
            QPointF newP2 = (m_shapeEndpointDragging == 1) ? imgPos : am.p2;
            emit shapeLineEndpointsChanged(m_activeShapeIndex, newP1, newP2);
        } else if (m_shapeDrag == ShapeDrag::Moving) {
            QPointF delta = (QPointF(ev->pos()) - QPointF(m_shapeDragStartMouse)) / m_scale;
            emit shapeMoved(m_activeShapeIndex, delta);
        } else if (m_shapeDrag == ShapeDrag::Rotating) {
            QPointF anchor = m_topLeft + am.rect.center() * m_scale;
            double a0 = std::atan2(m_shapeDragStartMouse.y() - anchor.y(),
                                   m_shapeDragStartMouse.x() - anchor.x());
            double a1 = std::atan2(ev->pos().y() - anchor.y(), ev->pos().x() - anchor.x());
            double deltaDeg = (a1 - a0) * 180.0 / M_PI;
            emit shapeRotated(m_activeShapeIndex, m_shapeRotateStartAngle + deltaDeg);
        } else { // Resizing
            QPointF anchor = am.rect.center();
            QTransform t;
            t.translate(anchor.x(), anchor.y());
            t.rotate(-am.rotation);
            t.translate(-anchor.x(), -anchor.y());
            QPointF localMouse = t.map((QPointF(ev->pos()) - m_topLeft) / m_scale);
            QRectF r = am.rect;

            // The corner opposite the one being dragged stays fixed.
            QPointF fixedCorner;
            switch (m_shapeResizeCorner) {
                case Handle::TopLeft:     fixedCorner = r.bottomRight(); break;
                case Handle::TopRight:    fixedCorner = r.bottomLeft(); break;
                case Handle::BottomLeft:  fixedCorner = r.topRight(); break;
                case Handle::BottomRight: fixedCorner = r.topLeft(); break;
                default:                  fixedCorner = r.topLeft(); break;
            }

            QPointF newCorner = localMouse;
            if ((ev->modifiers() & Qt::ShiftModifier) && m_shapeResizeStartSize.width() > 0 &&
                m_shapeResizeStartSize.height() > 0) {
                // Lock to the shape's aspect ratio at the start of this drag,
                // driven by whichever axis the mouse has moved further along.
                double aspect = m_shapeResizeStartSize.width() / m_shapeResizeStartSize.height();
                double dx = localMouse.x() - fixedCorner.x();
                double dy = localMouse.y() - fixedCorner.y();
                if (std::abs(dx) > std::abs(dy) * aspect)
                    dy = std::copysign(std::abs(dx) / aspect, dy != 0 ? dy : dx);
                else
                    dx = std::copysign(std::abs(dy) * aspect, dx != 0 ? dx : dy);
                newCorner = fixedCorner + QPointF(dx, dy);
            }

            switch (m_shapeResizeCorner) {
                case Handle::TopLeft:     r.setTopLeft(newCorner); break;
                case Handle::TopRight:    r.setTopRight(newCorner); break;
                case Handle::BottomLeft:  r.setBottomLeft(newCorner); break;
                case Handle::BottomRight: r.setBottomRight(newCorner); break;
                default: break;
            }
            emit shapeResized(m_activeShapeIndex, r.normalized());
        }
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
    } else if (m_drag == Drag::Rotating) {
        QPointF anchor = m_rectAtDragStart.center();
        double a0 = std::atan2(m_cropDragStartMouse.y() - anchor.y(),
                               m_cropDragStartMouse.x() - anchor.x());
        double a1 = std::atan2(ev->pos().y() - anchor.y(), ev->pos().x() - anchor.x());
        double deltaDeg = (a1 - a0) * 180.0 / M_PI;
        m_cropAngle = m_cropRotateStartAngle + deltaDeg;
        update();
    } else if (m_drag == Drag::Resizing) {
        QRect tr = targetRect();
        QRect r = m_rectAtDragStart;
        // Unrotate the mouse into the rect's local frame using the FIXED
        // drag-start center (not the live selectionRect(), which is what
        // we're actively resizing) so the pivot doesn't shift mid-drag.
        QPoint pos = ev->pos();
        if (m_cropAngle != 0.0) {
            QPointF center = r.center();
            QTransform t;
            t.translate(center.x(), center.y());
            t.rotate(-m_cropAngle);
            t.translate(-center.x(), -center.y());
            pos = t.map(QPointF(ev->pos())).toPoint();
        }
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
    } else if (m_bucketMode) {
        // Re-assert the bucket cursor on every hover so nothing (mask-panel
        // refresh, focus changes, etc.) can silently revert it to the arrow
        // between fills; the tool otherwise stays active until the user
        // explicitly switches tools or presses Esc.
        setCursor(bucketCursor());
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
                // cropInRotateZone also yields CrossCursor, same as the
                // default — matches the rotate-handle cursor convention used
                // by the text/shape tools elsewhere in this function.
                c = cropRectContains(ev->pos()) ? Qt::SizeAllCursor : Qt::CrossCursor;
                break;
        }
        setCursor(c);
    } else if (m_textMode) {
        Qt::CursorShape c = Qt::IBeamCursor;
        if (m_activeTextIndex >= 0 && m_activeTextIndex < m_textMarkers.size()) {
            const TextMarker &am = m_textMarkers[m_activeTextIndex];
            if ((QPointF(ev->pos()) - textRotateHandlePos(am)).manhattanLength() <= 10)
                c = Qt::CrossCursor;
            else if (Handle h = textCornerHandleAt(ev->pos()); h != Handle::None)
                c = (h == Handle::TopRight || h == Handle::BottomLeft) ? Qt::SizeBDiagCursor
                                                                       : Qt::SizeFDiagCursor;
            else if (textMarkerAt(ev->pos()) >= 0)
                c = Qt::SizeAllCursor;
        } else if (textMarkerAt(ev->pos()) >= 0) {
            c = Qt::SizeAllCursor;
        }
        setCursor(c);
    } else if (m_shapeMode) {
        Qt::CursorShape c = Qt::CrossCursor;
        if (m_selectedShapeIndices.size() > 1 && shapeGroupCornerHandleAt(ev->pos()) != Handle::None) {
            Handle h = shapeGroupCornerHandleAt(ev->pos());
            c = (h == Handle::TopRight || h == Handle::BottomLeft) ? Qt::SizeBDiagCursor
                                                                    : Qt::SizeFDiagCursor;
        } else if (m_activeShapeIndex >= 0 && m_activeShapeIndex < m_shapeMarkers.size()) {
            const ShapeMarker &am = m_shapeMarkers[m_activeShapeIndex];
            if (am.type == ShapeType::Line && shapeEndpointAt(ev->pos()) >= 0) {
                c = Qt::SizeAllCursor;
            } else if (am.type != ShapeType::Line &&
                       (QPointF(ev->pos()) - shapeRotateHandlePos(am)).manhattanLength() <= 10) {
                c = Qt::CrossCursor;
            } else if (Handle h = shapeCornerHandleAt(ev->pos()); h != Handle::None) {
                c = (h == Handle::TopRight || h == Handle::BottomLeft) ? Qt::SizeBDiagCursor
                                                                       : Qt::SizeFDiagCursor;
            } else if (shapeMarkerAt(ev->pos()) >= 0) {
                c = Qt::SizeAllCursor;
            }
        } else if (shapeMarkerAt(ev->pos()) >= 0) {
            c = Qt::SizeAllCursor;
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
    if (m_guideDrag != GuideDrag::None && ev->button() == Qt::LeftButton) {
        const int kThickness = 20;
        switch (m_guideDrag) {
        case GuideDrag::NewH:
            if (ev->pos().y() >= kThickness) { // dropped on canvas: keep it
                m_guidesH.append((ev->pos().y() - m_topLeft.y()) / m_scale / std::max(1, m_img.height()));
                emitGuidesChanged();
            }
            break;
        case GuideDrag::NewV:
            if (ev->pos().x() >= kThickness) {
                m_guidesV.append((ev->pos().x() - m_topLeft.x()) / m_scale / std::max(1, m_img.width()));
                emitGuidesChanged();
            }
            break;
        case GuideDrag::MoveH:
            if (ev->pos().y() < kThickness) m_guidesH.remove(m_guideDragIndex); // dropped back on ruler: delete
            emitGuidesChanged();
            break;
        case GuideDrag::MoveV:
            if (ev->pos().x() < kThickness) m_guidesV.remove(m_guideDragIndex);
            emitGuidesChanged();
            break;
        default: break;
        }
        m_guideDrag = GuideDrag::None;
        m_guideDragIndex = -1;
        unsetCursor();
        update();
        return;
    }
    if (m_shapeCtrlPending && ev->button() == Qt::LeftButton) {
        // Released without moving past the duplicate-drag threshold: a plain
        // Ctrl+click, toggling multi-selection membership.
        m_shapeCtrlPending = false;
        emit shapeToggleSelectRequested(m_shapeCtrlPendingHit);
        update();
        return;
    }
    if (m_shapeDrag == ShapeDrag::Creating && ev->button() == Qt::LeftButton) {
        m_shapeDrag = ShapeDrag::None;
        QRect box = QRect(m_shapeCreateP0, m_shapeCreateP1).normalized();
        update();
        if (box.width() >= 4 || box.height() >= 4) {
            QPointF tl = (QPointF(box.topLeft()) - m_topLeft) / m_scale;
            QPointF br = (QPointF(box.bottomRight()) - m_topLeft) / m_scale;
            emit shapeCreateRequested(m_activeShapeType, QRectF(tl, br).normalized());
        }
        return;
    }
    if (m_shapeDrag != ShapeDrag::None && ev->button() == Qt::LeftButton) {
        m_shapeDrag = ShapeDrag::None;
        m_shapeEndpointDragging = -1;
        update();
        return;
    }
    if (m_textDrag != TextDrag::None && ev->button() == Qt::LeftButton) {
        m_textDrag = TextDrag::None;
        m_activeGuideXs.clear();
        m_activeGuideYs.clear();
        update();
        return;
    }
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
    if (m_removeObjectDragging && ev->button() == Qt::LeftButton) {
        m_removeObjectDragging = false;
        emit removeObjectFinished();
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
        emit cropSelected(selectionInImage(), m_cropAngle);
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

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent *ev) {
    if (m_textMode && ev->button() == Qt::LeftButton) {
        int hit = textMarkerAt(ev->pos());
        if (hit >= 0) {
            m_activeTextIndex = hit;
            emit textSelected(hit);
            emit textEditRequested(hit);
            update();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(ev);
}

bool ImageCanvas::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_textEditor && m_textEditor) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape) {
                cancelTextEditor();
                return true;
            }
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
                !(ke->modifiers() & Qt::ShiftModifier)) {
                commitTextEditor();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            commitTextEditor();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ImageCanvas::wheelEvent(QWheelEvent *ev) {
    if ((ev->modifiers() & Qt::ControlModifier) && !m_img.isNull()) {
        // In heal mode, ctrl+wheel resizes the brush instead of zooming.
        if (m_healMode) {
            double notches = ev->angleDelta().y() / 120.0;
            int step = int(std::lround(notches * kHealBrushStepPerNotch));
            m_brushRadius = std::clamp(m_brushRadius + step, kHealBrushMin, kHealBrushMax);
            emit healBrushRadiusChanged(m_brushRadius);
            update();
            ev->accept();
            return;
        }
        // In erase mode, ctrl+wheel resizes the erase brush the same way.
        if (m_eraseMode) {
            double notches = ev->angleDelta().y() / 120.0;
            int step = int(std::lround(notches * kHealBrushStepPerNotch));
            m_brushRadius = std::clamp(m_brushRadius + step, kHealBrushMin, kHealBrushMax);
            emit eraseBrushRadiusChanged(m_brushRadius);
            update();
            ev->accept();
            return;
        }
        // In remove-object mode, ctrl+wheel resizes its brush the same way.
        if (m_removeObjectMode) {
            double notches = ev->angleDelta().y() / 120.0;
            int step = int(std::lround(notches * kHealBrushStepPerNotch));
            m_brushRadius = std::clamp(m_brushRadius + step, kHealBrushMin, kHealBrushMax);
            emit removeObjectBrushRadiusChanged(m_brushRadius);
            update();
            ev->accept();
            return;
        }
        // In brush-mask mode, ctrl+wheel resizes the mask brush the same way.
        if (m_maskMode && (m_maskKind == MaskType::Brush || m_maskKind == MaskType::Paint)) {
            double notches = ev->angleDelta().y() / 120.0;
            double r = std::clamp(m_activeMask.brushRadius + notches * kMaskBrushStep,
                                  kMaskBrushMin, kMaskBrushMax);
            emit maskBrushRadiusChanged(r);
            update();
            ev->accept();
            return;
        }
        // In text mode with a text selected, ctrl+wheel scales its font size
        // the same way dragging a corner handle does — each notch is one
        // resize gesture (start captures the current size as the baseline,
        // then scales it by a fixed ~5% step).
        if (m_textMode && m_activeTextIndex >= 0 && m_activeTextIndex < m_textMarkers.size()) {
            double factor = ev->angleDelta().y() > 0 ? 1.05 : (1.0 / 1.05);
            emit textResizeStarted(m_activeTextIndex);
            emit textResized(m_activeTextIndex, factor);
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
    if (m_textMode && (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) &&
        m_activeTextIndex >= 0) {
        int idx = m_activeTextIndex;
        m_activeTextIndex = -1;
        emit textDeleteRequested(idx);
        ev->accept();
        return;
    }
    if (m_shapeMode && (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) &&
        m_selectedShapeIndices.size() > 1) {
        QList<int> indices = m_selectedShapeIndices.values();
        m_activeShapeIndex = -1;
        m_selectedShapeIndices.clear();
        emit shapeGroupDeleteRequested(indices);
        ev->accept();
        return;
    }
    if (m_shapeMode && (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) &&
        m_activeShapeIndex >= 0) {
        int idx = m_activeShapeIndex;
        m_activeShapeIndex = -1;
        emit shapeDeleteRequested(idx);
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
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_backgroundColor);
    setPalette(pal);
    QSettings().setValue("edit/canvasBackgroundColor", m_backgroundColor);
    update();
    emit backgroundColorChanged(m_backgroundColor);
}

void ImageCanvas::setShowCheckerboard(bool on) {
    if (m_showCheckerboard == on) return;
    m_showCheckerboard = on;
    update();
}

void ImageCanvas::setShowRulers(bool on) {
    if (m_showRulers == on) return;
    m_showRulers = on;
    update();
}

void ImageCanvas::contextMenuEvent(QContextMenuEvent *ev) {
    if (m_showRulers && !m_img.isNull()) {
        int hh = guideHAt(ev->pos());
        int hv = hh < 0 ? guideVAt(ev->pos()) : -1;
        if (hh >= 0 || hv >= 0 || !m_guidesH.isEmpty() || !m_guidesV.isEmpty()) {
            QMenu menu(this);
            QAction *del = (hh >= 0 || hv >= 0) ? menu.addAction(tr("Delete Guide")) : nullptr;
            QAction *clearAll = (!m_guidesH.isEmpty() || !m_guidesV.isEmpty())
                                     ? menu.addAction(tr("Clear All Guides")) : nullptr;
            QAction *chosen = menu.exec(ev->globalPos());
            if (chosen && chosen == del) {
                if (hh >= 0) m_guidesH.remove(hh); else m_guidesV.remove(hv);
                emitGuidesChanged();
                update();
            } else if (chosen && chosen == clearAll) {
                m_guidesH.clear();
                m_guidesV.clear();
                emitGuidesChanged();
                update();
            }
            return;
        }
    }

    QRect target = targetRect();
    if (target.contains(ev->pos())) {
        ev->ignore();
        return;
    }

    static const QColor kDefaultBackground(30, 30, 30);
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    struct Item { const char *label; QColor color; };
    const Item items[] = {
        {"White", QColor(255, 255, 255)},
        {"Light Gray", QColor(200, 200, 200)},
        {"Gray", QColor(128, 128, 128)},
        {"Dark Gray", QColor(64, 64, 64)},
        {"Black", QColor(0, 0, 0)},
    };
    for (const auto &it : items) {
        QAction *a = menu.addAction(it.label);
        a->setCheckable(true);
        a->setChecked(m_backgroundColor == it.color);
        group->addAction(a);
        QColor c = it.color;
        connect(a, &QAction::triggered, this, [this, c]() { setBackgroundColor(c); });
    }
    menu.addSeparator();
    QAction *custom = menu.addAction(tr("Custom..."));
    QAction *reset = menu.addAction(tr("Reset to Default"));

    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == custom) {
        QColor c = QColorDialog::getColor(m_backgroundColor, this, tr("Canvas Background Color"));
        if (c.isValid()) setBackgroundColor(c);
    } else if (chosen == reset) {
        setBackgroundColor(kDefaultBackground);
    }
}
