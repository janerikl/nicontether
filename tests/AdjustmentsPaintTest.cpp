#include "edit/Adjustments.h"
#include "edit/EditSidecar.h"

#include <QImage>
#include <QTemporaryDir>
#include <QFile>
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

    // Image layers can be panned and resized within the frame.
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        QImage src(4, 4, QImage::Format_ARGB32);
        src.fill(QColor(255, 0, 0));

        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = "layer.png";
        layer.sourceImageCache = src;
        layer.opacity = 1.0;

        Adjustments adj;
        adj.masks.append(layer);

        adj.masks[0].sourceImageOffset = QPointF(0.0, -1.0);
        QImage shiftedUp = applyAdjustments(base, adj);
        assert(qRed(shiftedUp.pixel(1, 0)) > 200);

        adj.masks[0].sourceImageOffset = QPointF(0.0, 1.0);
        QImage shiftedDown = applyAdjustments(base, adj);
        assert(qRed(shiftedDown.pixel(1, 3)) > 200);

        adj.masks[0].sourceImageOffset = QPointF(0.0, 0.0);
        adj.masks[0].sourceImageScale = QPointF(0.5, 0.5);
        QImage resized = applyAdjustments(base, adj);
        assert(qRed(resized.pixel(2, 2)) > 200);
        assert(qRed(resized.pixel(0, 0)) == 0);
    }

    // Image-layer position persists through the sidecar format.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = path;
        layer.sourceImageOffset = QPointF(0.25, -0.5);
        layer.sourceImageScale = QPointF(0.75, 0.5);
        layer.sourceImageLockRatio = false;
        adj.masks.append(layer);

        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.masks.size() == 1);
        assert(loaded.masks[0].sourceImageOffset == QPointF(0.25, -0.5));
        assert(loaded.masks[0].sourceImageScale == QPointF(0.75, 0.5));
        assert(!loaded.masks[0].sourceImageLockRatio);
    }

    // Canvas background color persists through the sidecar format.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        adj.backgroundColor = QColor(0x11, 0x22, 0x33);
        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.backgroundColor == QColor(0x11, 0x22, 0x33));
    }

    // A sidecar written before the background-color field existed (or one
    // simply missing it) loads the default instead of an invalid color.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        QFile f(EditSidecar::pathFor(path));
        assert(f.open(QIODevice::WriteOnly));
        f.write("{\"version\":5,\"brightness\":0}");
        f.close();

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.backgroundColor == QColor(30, 30, 30));
    }

    // Incremental brush-rasterization cache: rendering a growing stroke one
    // point at a time through a shared BrushRasterCache (as RenderWorker does
    // during a live drag) must match a from-scratch render of the same
    // stroke at every step, including after a simulated undo (stroke
    // shrinks, forcing the cache to rebuild) and across erase dabs.
    {
        QImage base(20, 20, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask brush;
        brush.type = MaskType::Brush;
        brush.brushRadius = 0.15;
        brush.hardness = 0.6;
        brush.adj.brightness = 40; // gives the masked region visible content to compare

        Adjustments adj;
        adj.masks.append(brush);

        QVector<BrushRasterCache> cache;
        QVector<QPointF> pts = {{0.2, 0.2}, {0.3, 0.25}, {0.4, 0.3}, {0.5, 0.35},
                                {0.6, 0.4}, {0.5, 0.5}, {0.4, 0.6}};
        for (int i = 0; i < pts.size(); ++i) {
            bool erase = (i == 5); // one erase dab partway through
            adj.masks[0].stroke.append(BrushStrokePoint{pts[i], erase});

            QImage incremental = applyAdjustments(base, adj, &cache);
            QImage fromScratch = applyAdjustments(base, adj, nullptr);
            assert(incremental == fromScratch);
        }

        // Simulate an undo: stroke shrinks. The cache must detect this and
        // rebuild rather than silently reusing stale coverage.
        adj.masks[0].stroke.resize(3);
        QImage afterUndoIncremental = applyAdjustments(base, adj, &cache);
        QImage afterUndoFromScratch = applyAdjustments(base, adj, nullptr);
        assert(afterUndoIncremental == afterUndoFromScratch);

        // Resume painting after the undo — cache must extend correctly again.
        adj.masks[0].stroke.append(BrushStrokePoint{{0.55, 0.45}, false});
        QImage resumedIncremental = applyAdjustments(base, adj, &cache);
        QImage resumedFromScratch = applyAdjustments(base, adj, nullptr);
        assert(resumedIncremental == resumedFromScratch);
    }

    std::printf("AdjustmentsPaintTest: all assertions passed\n");
    return 0;
}
