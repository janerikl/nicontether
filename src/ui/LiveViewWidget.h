#pragma once

#include <QWidget>
#include <QImage>

// Displays the live view stream. Emits focusRequested with sensor-space pixel
// coordinates when the user clicks, for click-to-focus.
class LiveViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit LiveViewWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void clearFrame();

signals:
    void focusRequested(int sensorX, int sensorY);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect drawnRect() const; // where the frame is painted (letterboxed)

    QImage m_frame;
};
