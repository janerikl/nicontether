#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include "edit/Adjustments.h"

// Draws one shape op, already in `img`'s local pixel space (geometry
// pre-offset and pre-scaled). Used by the masks-based rendering path in
// Adjustments.cpp (via maskToShapeOp) for MaskType::Shape layers.
void applyShapeOp(QImage &img, const ShapeOp &op);

// Unrotated bounding box of `op`'s shape, in `op`'s own pixel space. Used to
// hit-test/draw the selection box on the canvas; ignores rotation, which
// callers apply separately about the box's center.
QRectF shapeOpBounds(const ShapeOp &op);
