#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include "edit/Adjustments.h"

// Draws text overlays onto `img` in place, as the very last compositing step
// (after tone/colour/vignette) so text pixels are never affected by those
// adjustments. `texts` are stored in oriented-image pixel space, pre-crop
// (see TextOp); `orientedToGeom` maps that space into the (unscaled) crop
// already baked into `img` (identity if uncropped) — see
// RetouchTab::m_orientedToGeom. `geomRotationDeg` is the straighten angle of
// that same crop (0 if none/unrotated), added to each op's own rotation so
// text keeps its orientation relative to the (now-rotated) photo content.
// `scale` uniformly resizes position/font/outline/shadow metrics (1.0 for
// full-res/export, <1.0 for a display-scaled preview).
void applyTexts(QImage &img, const QVector<TextOp> &texts,
                 const QTransform &orientedToGeom = QTransform(),
                 double geomRotationDeg = 0.0, double scale = 1.0);

// Draws one text op, already in `img`'s local pixel space (position/metrics
// pre-offset and pre-scaled — see applyTexts).
void applyTextOp(QImage &img, const TextOp &op);

// Unrotated bounding box of `op`'s text block, in `op`'s own pixel space
// (i.e. QRectF(op.pos, size) — top-left is op.pos). Used to hit-test/draw the
// selection box on the canvas; ignores rotation, which callers apply
// separately about op.pos.
QRectF textOpBounds(const TextOp &op);
