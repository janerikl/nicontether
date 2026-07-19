#include "edit/Adjustments.h"

#include <QImage>
#include <cassert>
#include <cstdio>

int main() {
    // A tiny black base image with one full-coverage red Paint layer should
    // come out red (opacity 1, Normal blend, hardness 1 covers the whole
    // frame from a single centered dab with a huge radius).
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(255, 0, 0);
        paint.brushRadius = 2.0; // width-normalized; radius = 2*W covers a 4x4 image entirely
        paint.hardness = 1.0;
        paint.opacity = 1.0;
        paint.blend = BlendMode::Normal;
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        QRgb center = out.pixel(2, 2);
        assert(qRed(center) > 250 && qGreen(center) < 5 && qBlue(center) < 5);
    }

    // An empty-stroke Paint layer contributes nothing (mirrors the existing
    // MaskType::Brush empty-stroke skip).
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(255, 0, 0);
        paint.opacity = 1.0;

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        QRgb center = out.pixel(2, 2);
        assert(qRed(center) == 0 && qGreen(center) == 0 && qBlue(center) == 0);
    }

    // Opacity 0.5 blends halfway between base and paint color.
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(200, 0, 0);
        paint.brushRadius = 2.0;
        paint.hardness = 1.0;
        paint.opacity = 0.5;
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        int r = qRed(out.pixel(2, 2));
        assert(r > 90 && r < 110); // ~100, halfway between 0 and 200
    }

    std::printf("AdjustmentsPaintTest: all assertions passed\n");
    return 0;
}
