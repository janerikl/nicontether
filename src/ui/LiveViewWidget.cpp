#include "ui/LiveViewWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>
#include <QMenu>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QSettings>

#include "ui/AfMapping.h"

LiveViewWidget::LiveViewWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(640, 426);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    m_gridMode = GridMode(QSettings().value("liveview/gridMode",
                                            int(GridMode::Off)).toInt());
}

void LiveViewWidget::setFrame(const QImage &frame) {
    m_frame = frame;
    update();
}

void LiveViewWidget::clearFrame() {
    m_frame = QImage();
    update();
}

void LiveViewWidget::setAfFrameSize(int w, int h) {
    m_afFrameW = w;
    m_afFrameH = h;
}

void LiveViewWidget::setAfResult(bool ok) {
    if (!m_hasReticle) return;
    m_afState = ok ? AfState::Ok : AfState::Failed;
    update();
}

void LiveViewWidget::clearReticle() {
    m_hasReticle = false;
    update();
}

void LiveViewWidget::setCalibrationMode(bool on) {
    m_calibrating = on;
    if (!on) {
        m_hasCrosshair = false;
    } else {
        // Hide the normal AF reticle while calibrating.
        m_hasReticle = false;
    }
    update();
}

void LiveViewWidget::setCalibrationCrosshair(bool on, QPointF norm) {
    m_hasCrosshair = on;
    m_crosshairNorm = norm;
    update();
}

void LiveViewWidget::setGridMode(GridMode m) {
    if (m_gridMode == m) return;
    m_gridMode = m;
    QSettings().setValue("liveview/gridMode", int(m));
    update();
}

void LiveViewWidget::drawGrid(QPainter &painter, const QRect &r) const {
    auto segs = grid::segments(m_gridMode);
    if (segs.empty()) return;
    painter.save();
    painter.setClipRect(r);
    painter.setBrush(Qt::NoBrush);
    // Two-pass stroke: dark under-stroke for contrast, then bright line.
    const QColor dark(0, 0, 0, 90);
    const QColor light(255, 255, 255, 180);
    auto toLine = [&](const grid::Seg &s) {
        return QLineF(r.x() + s.x1 * r.width(), r.y() + s.y1 * r.height(),
                      r.x() + s.x2 * r.width(), r.y() + s.y2 * r.height());
    };
    painter.setPen(QPen(dark, 3));
    for (const auto &s : segs) painter.drawLine(toLine(s));
    painter.setPen(QPen(light, 1));
    for (const auto &s : segs) painter.drawLine(toLine(s));
    painter.restore();
}

QRect LiveViewWidget::drawnRect() const {
    if (m_frame.isNull()) return QRect();
    QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
    int x = (width() - scaled.width()) / 2;
    int y = (height() - scaled.height()) / 2;
    return QRect(x, y, scaled.width(), scaled.height());
}

void LiveViewWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_frame.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
                         "Live view off\nConnect a camera and press Live View");
        return;
    }
    QRect r = drawnRect();
    painter.drawImage(r, m_frame);

    drawGrid(painter, r);

    if (m_hasReticle) {
        int side = int(m_afBoxFrac * qMin(r.width(), r.height()));
        QPointF c(r.x() + m_reticleNorm.x() * r.width(),
                  r.y() + m_reticleNorm.y() * r.height());
        QRectF box(c.x() - side / 2.0, c.y() - side / 2.0, side, side);
        QColor color = m_afState == AfState::Ok       ? QColor(0, 200, 0)
                       : m_afState == AfState::Failed  ? QColor(220, 0, 0)
                                                       : QColor(230, 200, 0);
        QPen pen(color, 2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);
    }

    if (m_hasCrosshair) {
        QPointF c(r.x() + m_crosshairNorm.x() * r.width(),
                  r.y() + m_crosshairNorm.y() * r.height());
        QPen pen(QColor(80, 180, 255), 2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        int rad = 14;
        painter.drawEllipse(c, rad, rad);
        painter.drawLine(QPointF(c.x() - rad - 6, c.y()), QPointF(c.x() + rad + 6, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - rad - 6), QPointF(c.x(), c.y() + rad + 6));
    }
}

void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;

    if (m_calibrating) {
        double nx = double(ev->pos().x() - r.x()) / r.width();
        double ny = double(ev->pos().y() - r.y()) / r.height();
        m_crosshairNorm = QPointF(nx, ny);
        m_hasCrosshair = true;
        update();
        emit calibrationPointPicked(nx, ny);
        return;
    }

    // AF frame size, falling back to the decoded frame's own pixels if unset.
    int fw = m_afFrameW > 0 ? m_afFrameW : m_frame.width();
    int fh = m_afFrameH > 0 ? m_afFrameH : m_frame.height();

    afmap::Result af = afmap::mapClickToAf(ev->pos().x(), ev->pos().y(),
                                           r.x(), r.y(), r.width(), r.height(),
                                           fw, fh);
    if (!af.valid) return;

    // Arm a pending reticle at the normalized click position.
    m_reticleNorm = QPointF(double(ev->pos().x() - r.x()) / r.width(),
                            double(ev->pos().y() - r.y()) / r.height());
    m_afState = AfState::Pending;
    m_hasReticle = true;
    update();

    emit focusRequested(af.x, af.y);
}

void LiveViewWidget::contextMenuEvent(QContextMenuEvent *ev) {
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    struct Item { const char *label; GridMode mode; };
    const Item items[] = {
        {"Off",           GridMode::Off},
        {"Rule of Thirds",GridMode::Thirds},
        {"Golden Ratio",  GridMode::GoldenRatio},
        {"Golden Spiral", GridMode::GoldenSpiral},
        {"Center Crosshair", GridMode::Crosshair},
        {"Diagonals",     GridMode::Diagonals},
    };
    for (const auto &it : items) {
        QAction *a = menu.addAction(it.label);
        a->setCheckable(true);
        a->setChecked(m_gridMode == it.mode);
        group->addAction(a);
        GridMode m = it.mode;
        connect(a, &QAction::triggered, this, [this, m]() { setGridMode(m); });
    }
    menu.exec(ev->globalPos());
}
