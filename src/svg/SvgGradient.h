#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

enum class SvgGradientType { Linear, Radial };

struct SvgGradientStop {
    qreal position = 0.0; // 0..1
    QColor color = Qt::black;
};

struct SvgGradient {
    SvgGradientType type = SvgGradientType::Linear;
    QVector<SvgGradientStop> stops;
    // Linear: start/end in node-local unit space (0..1 of bounding box).
    QPointF linearStart{0.0, 0.0};
    QPointF linearEnd{1.0, 0.0};
    // Radial: center/focal + radius in node-local unit space.
    QPointF radialCenter{0.5, 0.5};
    QPointF radialFocal{0.5, 0.5};
    qreal radialRadius = 0.5;
};
