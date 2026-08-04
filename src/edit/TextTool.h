#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include "edit/Adjustments.h"

// Draws one text op, already in `img`'s local pixel space (position/metrics
// pre-offset and pre-scaled). Used by the masks-based rendering path in
// Adjustments.cpp (via maskToTextOp) for MaskType::TextBox layers.
void applyTextOp(QImage &img, const TextOp &op);

// Unrotated bounding box of `op`'s text block, in `op`'s own pixel space
// (i.e. QRectF(op.pos, size) — top-left is op.pos). Used to hit-test/draw the
// selection box on the canvas; ignores rotation, which callers apply
// separately about op.pos.
QRectF textOpBounds(const TextOp &op);
