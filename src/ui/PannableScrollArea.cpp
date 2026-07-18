#include "ui/PannableScrollArea.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>

PannableScrollArea::PannableScrollArea(QWidget *parent) : QScrollArea(parent) {
    // Needed so the widget receives key events (Space) directly.
    setFocusPolicy(Qt::StrongFocus);
}

void PannableScrollArea::keyPressEvent(QKeyEvent *ev) {
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat()) {
        m_spaceDown = true;
        updateCursor();
        ev->accept();
        return;
    }
    QScrollArea::keyPressEvent(ev);
}

void PannableScrollArea::keyReleaseEvent(QKeyEvent *ev) {
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat()) {
        m_spaceDown = false;
        if (!m_panning) updateCursor();
        ev->accept();
        return;
    }
    QScrollArea::keyReleaseEvent(ev);
}

void PannableScrollArea::mousePressEvent(QMouseEvent *ev) {
    if (m_spaceDown && ev->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPos = ev->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }
    QScrollArea::mousePressEvent(ev);
}

void PannableScrollArea::mouseMoveEvent(QMouseEvent *ev) {
    if (m_panning) {
        QPoint delta = ev->pos() - m_lastPos;
        m_lastPos = ev->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        ev->accept();
        return;
    }
    QScrollArea::mouseMoveEvent(ev);
}

void PannableScrollArea::mouseReleaseEvent(QMouseEvent *ev) {
    if (m_panning && ev->button() == Qt::LeftButton) {
        m_panning = false;
        updateCursor();
        ev->accept();
        return;
    }
    QScrollArea::mouseReleaseEvent(ev);
}

void PannableScrollArea::wheelEvent(QWheelEvent *ev) {
    int delta = ev->angleDelta().y();
    if (delta != 0) {
        // One notch is typically 120 units; report signed step count.
        int steps = delta > 0 ? 1 : -1;
        emit zoomStep(steps, ev->position().toPoint());
        ev->accept();
        return;
    }
    QScrollArea::wheelEvent(ev);
}

void PannableScrollArea::updateCursor() {
    if (m_spaceDown)
        viewport()->setCursor(Qt::OpenHandCursor);
    else
        viewport()->unsetCursor();
}
