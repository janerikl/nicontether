#include "ui/HistogramWidget.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kMargin = 8.0;
constexpr double kInputStrip = 14.0;  // triangle row under the histogram
constexpr double kBarGap = 4.0;
constexpr double kOutputBar = 12.0;   // output gradient bar height
constexpr double kOutputStrip = 14.0; // output triangle row
} // namespace

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(130);
    setMouseTracking(false);
}

void HistogramWidget::clear() {
    m_image = QImage();
    m_hasData = false;
    m_maxBin = 0;
    m_clipLow = m_clipHigh = false;
    update();
}

void HistogramWidget::setImage(const QImage &img) {
    m_image = img;
    if (img.isNull()) {
        clear();
        return;
    }
    compute();
    update();
}

void HistogramWidget::setDisplayChannel(DisplayChannel ch) {
    if (m_display == ch) return;
    m_display = ch;
    update();
}

void HistogramWidget::setLevelsChannel(const LevelsChannel &c) {
    m_ch = c;
    update();
}

void HistogramWidget::compute() {
    m_r.fill(0);
    m_g.fill(0);
    m_b.fill(0);
    m_luma.fill(0);
    if (m_image.isNull()) {
        m_hasData = false;
        return;
    }

    QImage src = (m_image.format() == QImage::Format_RGB32 ||
                  m_image.format() == QImage::Format_ARGB32)
                     ? m_image
                     : m_image.convertToFormat(QImage::Format_RGB32);
    const int w = src.width(), h = src.height();
    const qint64 total = qint64(w) * h;
    const qint64 budget = 200000;
    int step = 1;
    if (total > budget) step = int((total + budget - 1) / budget);

    for (int y = 0; y < h; y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            ++m_r[r];
            ++m_g[g];
            ++m_b[b];
            const int l = (r * 299 + g * 587 + b * 114) / 1000;
            ++m_luma[std::clamp(l, 0, 255)];
        }
    }

    m_maxBin = 0;
    for (int i = 1; i < 255; ++i) {
        m_maxBin = std::max(m_maxBin, m_r[i]);
        m_maxBin = std::max(m_maxBin, m_g[i]);
        m_maxBin = std::max(m_maxBin, m_b[i]);
    }
    if (m_maxBin == 0) m_maxBin = 1;
    const quint32 clipThresh = quint32(std::max<qint64>(1, m_maxBin / 4));
    m_clipLow = (m_luma[0] > clipThresh);
    m_clipHigh = (m_luma[255] > clipThresh);
    m_hasData = true;
}

QRectF HistogramWidget::graphRect() const {
    const double bottom = height() - kMargin - kOutputStrip - kOutputBar -
                          kBarGap - kInputStrip;
    return QRectF(kMargin, kMargin, width() - 2 * kMargin,
                  std::max(10.0, bottom - kMargin));
}
QRectF HistogramWidget::inputStripRect() const {
    const QRectF g = graphRect();
    return QRectF(g.left(), g.bottom(), g.width(), kInputStrip);
}
QRectF HistogramWidget::outputBarRect() const {
    const QRectF s = inputStripRect();
    return QRectF(s.left(), s.bottom() + kBarGap, s.width(), kOutputBar);
}

double HistogramWidget::xForValue(int v, const QRectF &area) const {
    return area.left() + (v / 255.0) * area.width();
}
int HistogramWidget::valueForX(double x, const QRectF &area) const {
    double t = (x - area.left()) / std::max(1.0, area.width());
    return std::clamp(int(std::lround(t * 255.0)), 0, 255);
}
double HistogramWidget::gammaHandleValue() const {
    // Handle sits where the tonal midpoint maps: m = 0.5^gamma of the input span.
    const double m = std::pow(0.5, std::clamp(m_ch.gamma, 0.1, 9.99));
    return m_ch.inBlack + m * (m_ch.inWhite - m_ch.inBlack);
}

static QPainterPath channelPath(const std::array<quint32, 256> &bins,
                                quint32 maxBin, const QRectF &area) {
    QPainterPath path;
    const double bw = area.width() / 256.0;
    path.moveTo(area.left(), area.bottom());
    for (int i = 0; i < 256; ++i) {
        double v = std::min(1.0, double(bins[i]) / double(maxBin));
        path.lineTo(area.left() + i * bw, area.bottom() - v * area.height());
    }
    path.lineTo(area.right(), area.bottom());
    path.closeSubpath();
    return path;
}

void HistogramWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 24));

    const QRectF g = graphRect();
    p.fillRect(g, QColor(18, 18, 18));
    if (!m_hasData) return;

    // Histogram.
    p.setPen(Qt::NoPen);
    if (m_display == Composite) {
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        struct Ch { const std::array<quint32, 256> *bins; QColor col; };
        const Ch chans[] = {{&m_r, QColor(200, 40, 40)},
                            {&m_g, QColor(40, 200, 40)},
                            {&m_b, QColor(60, 90, 220)}};
        for (const Ch &c : chans) {
            p.setBrush(c.col);
            p.drawPath(channelPath(*c.bins, m_maxBin, g));
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        const std::array<quint32, 256> *bins = &m_r;
        QColor col(200, 60, 60);
        if (m_display == Green) { bins = &m_g; col = QColor(60, 200, 60); }
        else if (m_display == Blue) { bins = &m_b; col = QColor(80, 110, 230); }
        p.setBrush(col);
        p.drawPath(channelPath(*bins, m_maxBin, g));
    }

    // Clipping markers on the graph edges.
    if (m_clipLow)
        p.fillRect(QRectF(g.left(), g.top(), 3, g.height()), QColor(120, 160, 255));
    if (m_clipHigh)
        p.fillRect(QRectF(g.right() - 3, g.top(), 3, g.height()), QColor(255, 120, 120));

    // Input handle strip.
    const QRectF in = inputStripRect();
    p.setPen(QPen(QColor(70, 70, 70), 1));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(in.left(), in.top()), QPointF(in.right(), in.top()));

    auto triangle = [&](double cx, double topY, const QColor &fill) {
        QPolygonF t;
        t << QPointF(cx, topY) << QPointF(cx - 5, topY + 10)
          << QPointF(cx + 5, topY + 10);
        p.setPen(QPen(QColor(20, 20, 20), 1));
        p.setBrush(fill);
        p.drawPolygon(t);
    };
    triangle(xForValue(m_ch.inBlack, in), in.top() + 1, QColor(20, 20, 20));
    triangle(xForValue(int(std::lround(gammaHandleValue())), in), in.top() + 1,
             QColor(140, 140, 140));
    triangle(xForValue(m_ch.inWhite, in), in.top() + 1, QColor(240, 240, 240));

    // Output gradient bar + handles.
    const QRectF bar = outputBarRect();
    QLinearGradient grad(bar.topLeft(), bar.topRight());
    grad.setColorAt(0.0, Qt::black);
    grad.setColorAt(1.0, Qt::white);
    p.setPen(QPen(QColor(70, 70, 70), 1));
    p.setBrush(grad);
    p.drawRect(bar);

    const QRectF outStrip(bar.left(), bar.bottom(), bar.width(), kOutputStrip);
    triangle(xForValue(m_ch.outBlack, outStrip), outStrip.top() + 1,
             QColor(20, 20, 20));
    triangle(xForValue(m_ch.outWhite, outStrip), outStrip.top() + 1,
             QColor(240, 240, 240));
}

HistogramWidget::Handle HistogramWidget::hitTest(const QPointF &pos) const {
    const QRectF in = inputStripRect();
    const QRectF bar = outputBarRect();
    const QRectF outStrip(bar.left(), bar.bottom(), bar.width(), kOutputStrip);

    auto near = [&](int v, const QRectF &area) {
        return std::abs(pos.x() - xForValue(v, area)) <= 7.0;
    };

    if (pos.y() >= in.top() - 2 && pos.y() <= in.bottom() + 2) {
        // Prefer the gamma handle when it overlaps the others.
        const int gv = int(std::lround(gammaHandleValue()));
        struct Cand { Handle h; int v; };
        const Cand cands[] = {{Gamma, gv}, {InBlack, m_ch.inBlack},
                              {InWhite, m_ch.inWhite}};
        Handle best = None;
        double bestDist = 8.0;
        for (const Cand &c : cands) {
            double d = std::abs(pos.x() - xForValue(c.v, in));
            if (d < bestDist) { bestDist = d; best = c.h; }
        }
        return best;
    }
    if (pos.y() >= outStrip.top() - 2 && pos.y() <= outStrip.bottom() + 2) {
        if (near(m_ch.outBlack, outStrip) &&
            std::abs(pos.x() - xForValue(m_ch.outBlack, outStrip)) <=
                std::abs(pos.x() - xForValue(m_ch.outWhite, outStrip)))
            return OutBlack;
        if (near(m_ch.outWhite, outStrip)) return OutWhite;
        if (near(m_ch.outBlack, outStrip)) return OutBlack;
    }
    return None;
}

void HistogramWidget::mousePressEvent(QMouseEvent *e) {
    if (!m_hasData) return;
    m_drag = hitTest(e->position());
    if (m_drag != None) e->accept();
}

void HistogramWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_drag == None) return;
    const QRectF in = inputStripRect();
    const QRectF bar = outputBarRect();
    const QRectF outStrip(bar.left(), bar.bottom(), bar.width(), kOutputStrip);
    const int v = valueForX(e->position().x(), in);

    switch (m_drag) {
    case InBlack:
        m_ch.inBlack = std::min(v, m_ch.inWhite - 1);
        break;
    case InWhite:
        m_ch.inWhite = std::max(v, m_ch.inBlack + 1);
        break;
    case Gamma: {
        double span = std::max(1, m_ch.inWhite - m_ch.inBlack);
        double m = (v - m_ch.inBlack) / span;
        m = std::clamp(m, 0.01, 0.99);
        // m = 0.5^gamma  ->  gamma = log(m)/log(0.5)
        m_ch.gamma = std::clamp(std::log(m) / std::log(0.5), 0.1, 9.99);
        break;
    }
    case OutBlack:
        m_ch.outBlack = std::min(valueForX(e->position().x(), outStrip),
                                 m_ch.outWhite - 1);
        break;
    case OutWhite:
        m_ch.outWhite = std::max(valueForX(e->position().x(), outStrip),
                                 m_ch.outBlack + 1);
        break;
    default:
        return;
    }
    update();
    emit channelEdited(m_ch);
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent *) { m_drag = None; }

QPair<int, int> HistogramWidget::autoRange() const {
    if (!m_hasData) return {0, 255};
    const std::array<quint32, 256> *bins = &m_luma;
    if (m_display == Red) bins = &m_r;
    else if (m_display == Green) bins = &m_g;
    else if (m_display == Blue) bins = &m_b;

    qint64 total = 0;
    for (quint32 c : *bins) total += c;
    if (total == 0) return {0, 255};
    const qint64 lowCut = total / 1000;        // 0.1%
    const qint64 highCut = total - total / 1000; // 99.9%

    int lo = 0, hi = 255;
    qint64 acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += (*bins)[i];
        if (acc > lowCut) { lo = i; break; }
    }
    acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += (*bins)[i];
        if (acc >= highCut) { hi = i; break; }
    }
    if (hi <= lo) hi = std::min(255, lo + 1);
    return {lo, hi};
}
