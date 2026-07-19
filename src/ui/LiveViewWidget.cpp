#include "ui/LiveViewWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

#include "ui/AfMapping.h"

LiveViewWidget::LiveViewWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(640, 426);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
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
}

void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;

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
