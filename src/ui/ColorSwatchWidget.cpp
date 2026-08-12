#include "ui/ColorSwatchWidget.h"

#include <QColorDialog>
#include <QMouseEvent>
#include <QPainter>
#include <utility>

ColorSwatchWidget::ColorSwatchWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(36, 36);
    setToolTip("Foreground/Background color — click a square to change it, "
              "X to swap, D to reset to black/white");
}

QRect ColorSwatchWidget::fgRect() const { return QRect(0, 0, 22, 22); }
QRect ColorSwatchWidget::bgRect() const { return QRect(12, 12, 22, 22); }
QRect ColorSwatchWidget::swapRect() const { return QRect(24, 0, 12, 12); }
QRect ColorSwatchWidget::resetRect() const { return QRect(0, 24, 12, 12); }

void ColorSwatchWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Background square (drawn first so the foreground square overlaps it).
    p.fillRect(bgRect(), m_bg);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(bgRect().adjusted(0, 0, -1, -1));

    // Foreground square.
    p.fillRect(fgRect(), m_fg);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(fgRect().adjusted(0, 0, -1, -1));

    // Swap arrow (small curved arrow glyph, top-right).
    p.setPen(QColor(200, 200, 200));
    QRect sr = swapRect();
    p.drawArc(sr.adjusted(1, 1, -1, -1), 30 * 16, 300 * 16);

    // Reset-to-default icon (tiny black/white squares, bottom-left).
    QRect rr = resetRect();
    p.fillRect(QRect(rr.left(), rr.top(), 6, 6), Qt::white);
    p.fillRect(QRect(rr.left() + 3, rr.top() + 3, 6, 6), Qt::black);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(QRect(rr.left(), rr.top(), 6, 6));
    p.drawRect(QRect(rr.left() + 3, rr.top() + 3, 6, 6));
}

void ColorSwatchWidget::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;
    const QPoint pos = ev->pos();

    if (swapRect().contains(pos)) {
        swapColors();
        return;
    }
    if (resetRect().contains(pos)) {
        resetColors();
        return;
    }
    if (fgRect().contains(pos)) {
        QColor c = QColorDialog::getColor(m_fg, this, "Foreground Color");
        if (c.isValid()) {
            m_fg = c;
            update();
            emit foregroundColorChanged(m_fg);
        }
        return;
    }
    if (bgRect().contains(pos)) {
        QColor c = QColorDialog::getColor(m_bg, this, "Background Color");
        if (c.isValid()) {
            m_bg = c;
            update();
            emit backgroundColorChanged(m_bg);
        }
        return;
    }
}

void ColorSwatchWidget::setForegroundColor(const QColor &color) {
    if (!color.isValid() || color == m_fg) return;
    m_fg = color;
    update();
    emit foregroundColorChanged(m_fg);
}

void ColorSwatchWidget::swapColors() {
    std::swap(m_fg, m_bg);
    update();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}

void ColorSwatchWidget::resetColors() {
    m_fg = Qt::black;
    m_bg = Qt::white;
    update();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}
