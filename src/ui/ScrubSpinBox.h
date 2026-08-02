#pragma once

#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QEvent>
#include <cstdlib>

// A QSpinBox that also supports Photoshop-style "scrubbing": click-dragging
// left/right anywhere on the field changes its value directly, without
// needing to type or use the up/down arrows. A plain click (no drag past a
// small threshold) still behaves like a normal spin box, so typing a value
// still works as usual.
// No Q_OBJECT: this class adds no signals/slots of its own (just overrides
// existing virtuals), so no moc step is needed.
class ScrubSpinBox : public QSpinBox {
public:
    explicit ScrubSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {
        lineEdit()->installEventFilter(this);
    }

    // How many pixels of horizontal drag correspond to one unit of value.
    void setScrubPixelsPerStep(int px) { m_pxPerStep = std::max(1, px); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != lineEdit()) return QSpinBox::eventFilter(watched, event);
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->pos();
                m_dragStartValue = value();
                m_dragging = false;
            }
            break; // still let the line edit process the press normally
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                int dx = me->pos().x() - m_dragStartPos.x();
                if (!m_dragging && std::abs(dx) > 4) {
                    m_dragging = true;
                    lineEdit()->setCursor(Qt::SizeHorCursor);
                    lineEdit()->deselect();
                }
                if (m_dragging) {
                    setValue(m_dragStartValue + dx / m_pxPerStep);
                    return true; // swallow — don't let the line edit move the text cursor
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (m_dragging) {
                m_dragging = false;
                lineEdit()->unsetCursor();
                return true; // swallow so releasing a scrub doesn't also start text-editing
            }
            break;
        }
        default:
            break;
        }
        return QSpinBox::eventFilter(watched, event);
    }

private:
    QPoint m_dragStartPos;
    int m_dragStartValue = 0;
    bool m_dragging = false;
    int m_pxPerStep = 4;
};

// Same click-drag scrubbing as ScrubSpinBox, for QDoubleSpinBox fields
// (outline width, shadow blur/opacity, background opacity/padding, etc).
class ScrubDoubleSpinBox : public QDoubleSpinBox {
public:
    explicit ScrubDoubleSpinBox(QWidget *parent = nullptr) : QDoubleSpinBox(parent) {
        lineEdit()->installEventFilter(this);
    }

    // How many pixels of horizontal drag correspond to one unit of value.
    void setScrubPixelsPerStep(double px) { m_pxPerStep = std::max(0.1, px); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != lineEdit()) return QDoubleSpinBox::eventFilter(watched, event);
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->pos();
                m_dragStartValue = value();
                m_dragging = false;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                int dx = me->pos().x() - m_dragStartPos.x();
                if (!m_dragging && std::abs(dx) > 4) {
                    m_dragging = true;
                    lineEdit()->setCursor(Qt::SizeHorCursor);
                    lineEdit()->deselect();
                }
                if (m_dragging) {
                    setValue(m_dragStartValue + dx / m_pxPerStep);
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (m_dragging) {
                m_dragging = false;
                lineEdit()->unsetCursor();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return QDoubleSpinBox::eventFilter(watched, event);
    }

private:
    QPoint m_dragStartPos;
    double m_dragStartValue = 0;
    bool m_dragging = false;
    double m_pxPerStep = 4.0;
};
