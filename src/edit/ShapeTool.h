#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include "edit/Adjustments.h"

// Draws shape overlays onto `img` in place, as the very last compositing
// step (right after text — see applyTexts). `shapes` are stored in
// oriented-image pixel space, pre-crop (see ShapeOp); `orientedToGeom` maps
// that space into the (unscaled) crop already baked into `img` (identity if
// uncropped) — see RetouchTab::m_orientedToGeom. `geomRotationDeg` is the
// straighten angle of that same crop (0 if none/unrotated): added to each
// non-Line op's own rotation so it keeps its orientation relative to the
// (now-rotated) photo content; Line endpoints don't need it since mapping
// p1/p2 through `orientedToGeom` already carries their rotation. `scale`
// uniformly resizes geometry/stroke width (1.0 for full-res/export, <1.0 for
// a display-scaled preview).
void applyShapes(QImage &img, const QVector<ShapeOp> &shapes,
                  const QTransform &orientedToGeom = QTransform(),
                  double geomRotationDeg = 0.0, double scale = 1.0);

// Draws one shape op, already in `img`'s local pixel space (geometry
// pre-offset and pre-scaled — see applyShapes).
void applyShapeOp(QImage &img, const ShapeOp &op);

// Unrotated bounding box of `op`'s shape, in `op`'s own pixel space. Used to
// hit-test/draw the selection box on the canvas; ignores rotation, which
// callers apply separately about the box's center.
QRectF shapeOpBounds(const ShapeOp &op);
