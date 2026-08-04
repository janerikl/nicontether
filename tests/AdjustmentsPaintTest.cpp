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
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false, paint.brushRadius,
                                             paint.hardness, paint.paintColor.rgb()});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        applyPaintMasks(out, adj.masks);
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
        applyPaintMasks(out, adj.masks);
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
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false, paint.brushRadius,
                                             paint.hardness, paint.paintColor.rgb()});

        Adjustments adj;
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        applyPaintMasks(out, adj.masks);
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

    // Erasing an image layer punches transparency through to the base below.
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(QColor(0, 0, 255)); // blue base, should show through the hole

        QImage src(8, 8, QImage::Format_ARGB32);
        src.fill(QColor(255, 0, 0)); // red layer, fully covering the frame

        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = "layer.png";
        layer.sourceImageCache = src;
        layer.opacity = 1.0;
        // Erase dab dead centre with a radius covering roughly the middle third.
        layer.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});

        Adjustments adj;
        adj.masks.append(layer);

        QImage out = applyAdjustments(base, adj);
        // Centre pixel: fully erased -> blue base shows through.
        assert(qRed(out.pixel(4, 4)) < 20 && qBlue(out.pixel(4, 4)) > 200);
        // Corner pixel: untouched by the erase dab -> still red.
        assert(qRed(out.pixel(0, 0)) > 200 && qBlue(out.pixel(0, 0)) < 20);
    }

    // Erase coverage is max-combined across overlapping dabs, not compounded
    // (two overlapping partial-feather dabs shouldn't erase more than a
    // single full-strength dab would at the same point).
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(QColor(0, 0, 255));

        QImage src(8, 8, QImage::Format_ARGB32);
        src.fill(QColor(255, 0, 0));

        Mask single;
        single.type = MaskType::None;
        single.sourceImagePath = "layer.png";
        single.sourceImageCache = src;
        single.opacity = 1.0;
        single.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});

        Mask doubled;
        doubled.type = MaskType::None;
        doubled.sourceImagePath = "layer.png";
        doubled.sourceImageCache = src;
        doubled.opacity = 1.0;
        doubled.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2});
        doubled.eraseStrokes.append(ErasePoint{QPointF(0.5, 0.5), 0.2}); // same spot again

        Adjustments adjSingle;
        adjSingle.masks.append(single);
        Adjustments adjDoubled;
        adjDoubled.masks.append(doubled);

        QImage outSingle = applyAdjustments(base, adjSingle);
        QImage outDoubled = applyAdjustments(base, adjDoubled);
        assert(outSingle.pixel(4, 4) == outDoubled.pixel(4, 4));
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

    // Erase strokes persist through the sidecar format.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = path;
        layer.eraseStrokes.append(ErasePoint{QPointF(0.3, 0.4), 0.1});
        layer.eraseStrokes.append(ErasePoint{QPointF(0.6, 0.7), 0.05});
        adj.masks.append(layer);

        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.masks.size() == 1);
        assert(loaded.masks[0].eraseStrokes.size() == 2);
        assert(loaded.masks[0].eraseStrokes[0].pt == QPointF(0.3, 0.4));
        assert(std::abs(loaded.masks[0].eraseStrokes[0].radius - 0.1) < 1e-9);
        assert(loaded.masks[0].eraseStrokes[1].pt == QPointF(0.6, 0.7));
        assert(std::abs(loaded.masks[0].eraseStrokes[1].radius - 0.05) < 1e-9);
    }

    // A sidecar written before eraseStrokes existed loads an empty list
    // instead of failing.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        Mask layer;
        layer.type = MaskType::None;
        layer.sourceImagePath = path;
        adj.masks.append(layer);
        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.masks.size() == 1);
        assert(loaded.masks[0].eraseStrokes.isEmpty());
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
            adj.masks[0].stroke.append(BrushStrokePoint{pts[i], erase, brush.brushRadius,
                                                        brush.hardness});

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
        adj.masks[0].stroke.append(BrushStrokePoint{{0.55, 0.45}, false, brush.brushRadius,
                                                    brush.hardness});
        QImage resumedIncremental = applyAdjustments(base, adj, &cache);
        QImage resumedFromScratch = applyAdjustments(base, adj, nullptr);
        assert(resumedIncremental == resumedFromScratch);
    }

    // Artificial lighting: a flat/uniform image must be unaffected regardless
    // of intensity/angle (verifies the flat-normal baseline is subtracted so
    // a constant light-elevation term doesn't uniformly shift brightness).
    {
        QImage base(40, 40, QImage::Format_ARGB32);
        base.fill(QColor(128, 128, 128));

        Adjustments adj;
        adj.lightAngle = 45;
        adj.lightIntensity = 80;

        QImage out = applyAdjustments(base, adj);
        QImage plain = base.convertToFormat(QImage::Format_RGBA64);
        for (int y = 10; y < 30; ++y)
            for (int x = 10; x < 30; ++x)
                assert(out.pixel(x, y) == plain.pixel(x, y));
    }

    // Zero intensity is a true no-op regardless of angle.
    {
        QImage base(40, 40, QImage::Format_ARGB32);
        base.fill(QColor(60, 90, 200));

        Adjustments adjOff;
        adjOff.lightAngle = 200;
        adjOff.lightIntensity = 0;
        Adjustments adjNone;

        QImage outOff = applyAdjustments(base, adjOff);
        QImage outNone = applyAdjustments(base, adjNone);
        assert(outOff == outNone);
    }

    // A hard vertical brightness step (simulated bump/edge) shades
    // asymmetrically depending on light direction, and flips with the sign
    // of intensity.
    {
        QImage base(40, 40, QImage::Format_ARGB32);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x)
                base.setPixel(x, y, (x < 20 ? QColor(60, 60, 60) : QColor(200, 200, 200)).rgb());

        auto meanNearEdge = [](const QImage &img) {
            long sum = 0;
            int n = 0;
            for (int y = 15; y < 25; ++y)
                for (int x = 17; x < 23; ++x) {
                    QRgba64 p = img.pixelColor(x, y).rgba64();
                    sum += p.red();
                    ++n;
                }
            return double(sum) / n;
        };

        Adjustments adjRight;
        adjRight.lightAngle = 0; // light from +x
        adjRight.lightIntensity = 90;
        Adjustments adjLeft;
        adjLeft.lightAngle = 180; // light from -x
        adjLeft.lightIntensity = 90;

        QImage outRight = applyAdjustments(base, adjRight);
        QImage outLeft = applyAdjustments(base, adjLeft);
        assert(meanNearEdge(outRight) != meanNearEdge(outLeft));

        Adjustments adjNeg = adjRight;
        adjNeg.lightIntensity = -90;
        QImage outNeg = applyAdjustments(base, adjNeg);
        // Flipping the sign of intensity should flip which side is favored,
        // i.e. move the edge brightness in the opposite direction from the
        // positive-intensity result relative to the untouched base.
        QImage outPlain = base.convertToFormat(QImage::Format_RGBA64);
        double baseline = meanNearEdge(outPlain);
        double diffPos = meanNearEdge(outRight) - baseline;
        double diffNeg = meanNearEdge(outNeg) - baseline;
        assert(diffPos * diffNeg <= 0.0);
    }

    // operator==/hasToneEdits/historyStepLabel recognize lighting fields.
    {
        Adjustments a, b;
        b.lightAngle = 30;
        b.lightIntensity = 50;
        assert(a != b);
        assert(hasToneEdits(b));
        assert(historyStepLabel(a, b) == QStringLiteral("Lighting"));
    }

    // Lighting fields round-trip through the sidecar format.
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString path = dir.filePath("photo.nef");

        Adjustments adj;
        adj.lightAngle = 275;
        adj.lightIntensity = -42;
        assert(EditSidecar::save(path, adj));

        Adjustments loaded;
        assert(EditSidecar::load(path, loaded));
        assert(loaded.lightAngle == 275);
        assert(loaded.lightIntensity == -42);
    }

    std::printf("AdjustmentsPaintTest: all assertions passed\n");
    return 0;
}
