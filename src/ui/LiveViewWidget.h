#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>

#include "ui/GridOverlay.h"

class QPainter;

// Displays the live view stream. Emits focusRequested with sensor-space pixel
// coordinates when the user clicks, for click-to-focus.
class LiveViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit LiveViewWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void clearFrame();

    // AF coordinate frame size (Nikon header ImageWidth/Height). When either is
    // <= 0, clicks fall back to the decoded frame's own pixel dimensions.
    void setAfFrameSize(int w, int h);

    // Calibration mode: clicks emit calibrationPointPicked instead of firing AF,
    // and a crosshair is drawn at the target position.
    void setCalibrationMode(bool on);
    void setCalibrationCrosshair(bool on, QPointF norm = {});

    void setGridMode(GridMode m);
    GridMode gridMode() const { return m_gridMode; }

public slots:
    // Update the reticle color after an AF-area command: green on success,
    // red on failure.
    void setAfResult(bool ok);
    // Remove the reticle (e.g. when live view stops).
    void clearReticle();

signals:
    void focusRequested(int sensorX, int sensorY);
    void calibrationPointPicked(double normX, double normY);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    QRect drawnRect() const; // where the frame is painted (letterboxed)

    QImage m_frame;
    int m_afFrameW = 0; // <= 0 => fall back to frame width
    int m_afFrameH = 0; // <= 0 => fall back to frame height

    enum class AfState { Pending, Ok, Failed };
    bool m_hasReticle = false;
    QPointF m_reticleNorm;               // 0..1 position within the drawn image
    AfState m_afState = AfState::Pending;
    double m_afBoxFrac = 0.12;           // reticle box size as fraction of min(drawn w,h)

    bool m_calibrating = false;
    bool m_hasCrosshair = false;
    QPointF m_crosshairNorm;

    GridMode m_gridMode = GridMode::Off;
    void drawGrid(QPainter &painter, const QRect &r) const;
};
