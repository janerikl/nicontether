#pragma once

#include <QScrollArea>
#include <QPoint>

// A QScrollArea with Photoshop-style panning: hold Space to get the hand
// cursor, then drag with the left mouse button to move around a zoomed image.
// Mouse-wheel motion is reported via zoomStep() for cursor-anchored zooming.
class PannableScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit PannableScrollArea(QWidget *parent = nullptr);

signals:
    // Emitted on wheel scroll. `steps` is positive when zooming in; `anchor`
    // is the cursor position in viewport coordinates.
    void zoomStep(int steps, const QPoint &anchor);

protected:
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    void updateCursor();

    bool m_spaceDown = false;
    bool m_panning = false;
    QPoint m_lastPos;
};
