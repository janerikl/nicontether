#pragma once

#include <QImage>
#include <QVector>

#include "edit/Adjustments.h"

// Applies spot-heal operations to `img` in place. Each op replaces a circular
// region with the cleanest (lowest-variance) nearby patch, feathered and
// brightness-matched so the seam disappears. Coordinates/radii are in the
// image's own pixel space (the oriented, full-resolution image).
void applyHeal(QImage &img, const QVector<HealOp> &ops);
void applyHealOp(QImage &img, const HealOp &op);
