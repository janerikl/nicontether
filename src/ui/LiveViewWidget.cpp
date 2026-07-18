#include "ui/LiveViewWidget.h"

#include <QPainter>
#include <QMouseEvent>

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
    painter.drawImage(drawnRect(), m_frame);
}

void LiveViewWidget::mousePressEvent(QMouseEvent *ev) {
    QRect r = drawnRect();
    if (m_frame.isNull() || !r.contains(ev->pos())) return;

    // Map widget click to source-frame pixel coordinates.
    double fx = double(ev->pos().x() - r.x()) / r.width();
    double fy = double(ev->pos().y() - r.y()) / r.height();
    int sx = int(fx * m_frame.width());
    int sy = int(fy * m_frame.height());
    emit focusRequested(sx, sy);
}
