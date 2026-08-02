#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QVector>

#include "edit/Adjustments.h"

// Draws shape overlays onto `img` in place, as the very last compositing
// step (right after text — see applyTexts). `shapes` are stored in
// oriented-image pixel space, pre-crop (see ShapeOp); `cropOffset` is the
// top-left of the crop already baked into `img` (QPoint() if uncropped),
// used to map each op's stored geometry into `img`'s local space. `scale`
// uniformly resizes geometry/stroke width (1.0 for full-res/export, <1.0 for
// a display-scaled preview).
void applyShapes(QImage &img, const QVector<ShapeOp> &shapes,
                  const QPoint &cropOffset = QPoint(), double scale = 1.0);

// Draws one shape op, already in `img`'s local pixel space (geometry
// pre-offset and pre-scaled — see applyShapes).
void applyShapeOp(QImage &img, const ShapeOp &op);

// Unrotated bounding box of `op`'s shape, in `op`'s own pixel space. Used to
// hit-test/draw the selection box on the canvas; ignores rotation, which
// callers apply separately about the box's center.
QRectF shapeOpBounds(const ShapeOp &op);
