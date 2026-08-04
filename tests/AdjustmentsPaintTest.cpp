#include "edit/Adjustments.h"
#include "edit/EditSidecar.h"

#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
    // MaskType::TextBox rendering goes through TextTool.cpp's QPainter/QFont
    // text-drawing path, which needs a QGuiApplication (font database) even
    // off-screen; force the offscreen platform so this runs headless in CI.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
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

    // masks[0] is documented as the topmost/frontmost entry: with two
    // full-frame, fully-opaque image layers stacked, the rendered result
    // must show whichever one sits at index 0, regardless of insertion
    // order. Regression test for a Layers-panel display-order bug where a
    // newly-added layer (always inserted at masks index 0) visually showed
    // up at the bottom of the panel list -- the panel bug didn't affect
    // this applyAdjustments/applyMasks compositing path itself, but this
    // pins down the masks[0]-is-topmost contract those UI pieces rely on.
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(Qt::black);

        QImage redSrc(4, 4, QImage::Format_ARGB32);
        redSrc.fill(QColor(255, 0, 0));
        QImage greenSrc(4, 4, QImage::Format_ARGB32);
        greenSrc.fill(QColor(0, 255, 0));

        Mask red;
        red.type = MaskType::None;
        red.sourceImagePath = "red.png";
        red.sourceImageCache = redSrc;
        red.opacity = 1.0;

        Mask green;
        green.type = MaskType::None;
        green.sourceImagePath = "green.png";
        green.sourceImageCache = greenSrc;
        green.opacity = 1.0;

        Adjustments adjRedOnTop;
        adjRedOnTop.masks.append(red);   // index 0: topmost
        adjRedOnTop.masks.append(green); // index 1: bottom
        QImage outRedOnTop = applyAdjustments(base, adjRedOnTop);
        assert(qRed(outRedOnTop.pixel(2, 2)) > 200);
        assert(qGreen(outRedOnTop.pixel(2, 2)) < 50);

        Adjustments adjGreenOnTop;
        adjGreenOnTop.masks.append(green); // index 0: topmost
        adjGreenOnTop.masks.append(red);   // index 1: bottom
        QImage outGreenOnTop = applyAdjustments(base, adjGreenOnTop);
        assert(qGreen(outGreenOnTop.pixel(2, 2)) > 200);
        assert(qRed(outGreenOnTop.pixel(2, 2)) < 50);
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
        // +1: a sidecar with no MaskType::Background entry and no legacy
        // backgroundHidden/backgroundDeleted fields gets one synthesized on
        // load (see EditSidecar::load's Background migration), appended
        // after this test's own single mask.
        assert(loaded.masks.size() == 2);
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
        assert(loaded.masks.size() == 2); // +1 synthesized Background, see above
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
        assert(loaded.masks.size() == 2); // +1 synthesized Background, see above
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

    // MaskType::Shape renders via the new masks-based interactive-tier path:
    // a full-frame red rectangle over a black base should turn the whole
    // frame red once both the (empty) static pass and the interactive pass
    // (applyPaintMasks) run — mirroring how RetouchTab::onRenderDone
    // composites applyAdjustments' cached buffer + applyPaintMasks. Built
    // directly via `masks` (not the old `shapes` array) so this doesn't
    // double-render.
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask shape;
        shape.type = MaskType::Shape;
        shape.shapeType = ShapeType::Rectangle;
        shape.shapeRect = QRectF(0, 0, 8, 8); // raw oriented-image pixel space, matches base size
        shape.shapeFillEnabled = true;
        shape.shapeFillColor = QColor(255, 0, 0);
        shape.shapeStrokeEnabled = false;
        shape.opacity = 1.0;
        shape.visible = true;

        Adjustments adj;
        adj.masks.append(shape);

        QImage out = applyAdjustments(base, adj);   // static tier: Shape excluded, stays black
        assert(qRed(out.pixel(4, 4)) == 0);
        applyPaintMasks(out, adj.masks);             // interactive tier: Shape composited here
        QRgb center = out.pixel(4, 4);
        assert(qRed(center) > 250 && qGreen(center) < 5 && qBlue(center) < 5);
    }

    // A hidden Shape mask contributes nothing, same as other mask types.
    {
        QImage base(8, 8, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask shape;
        shape.type = MaskType::Shape;
        shape.shapeType = ShapeType::Rectangle;
        shape.shapeRect = QRectF(0, 0, 8, 8);
        shape.shapeFillEnabled = true;
        shape.shapeFillColor = QColor(255, 0, 0);
        shape.visible = false;

        Adjustments adj;
        adj.masks.append(shape);

        QImage out = applyAdjustments(base, adj);
        applyPaintMasks(out, adj.masks);
        assert(qRed(out.pixel(4, 4)) == 0);
    }

    // MaskType::TextBox renders via the same new masks-based interactive-tier
    // path: solid background-box color should show through where the text
    // box is placed.
    {
        QImage base(20, 20, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask tb;
        tb.type = MaskType::TextBox;
        tb.textBoxPos = QPointF(0, 0);
        tb.textBoxText = QStringLiteral("Hi");
        tb.textBoxPixelSize = 12.0;
        tb.textBoxColor = QColor(255, 255, 255);
        tb.textBoxBgEnabled = true;
        tb.textBoxBgColor = QColor(0, 255, 0);
        tb.textBoxBgOpacity = 1.0;
        tb.textBoxBgPadding = 2.0;
        tb.opacity = 1.0;
        tb.visible = true;

        Adjustments adj;
        adj.masks.append(tb);

        QImage out = applyAdjustments(base, adj);
        assert(qGreen(out.pixel(1, 1)) == 0); // static tier: TextBox excluded, stays black
        applyPaintMasks(out, adj.masks);
        // Just below/right of the origin should be inside the padded
        // background box (text box top-left is (0,0), padding extends it).
        QRgb p = out.pixel(1, 1);
        assert(qGreen(p) > 200 && qRed(p) < 60 && qBlue(p) < 60);
    }

    // Paint, Shape, and TextBox composite together (true stack order) in a
    // single applyPaintMasks call, replacing the old separate
    // applyTexts/applyShapes/applyPaintMasks three-call sequence for masks-
    // based layers. Stack order (index 0 = top): TextBox on top of Shape on
    // top of Paint; each covers a distinct region so all three must appear.
    {
        QImage base(30, 10, QImage::Format_ARGB32);
        base.fill(Qt::black);

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(0, 0, 255);
        paint.brushRadius = 0.05; // small dab, width-normalized against 30px width
        paint.hardness = 1.0;
        paint.opacity = 1.0;
        // BrushStrokePoint coords are normalized by image WIDTH for both x
        // and y (see Mask::stroke doc comment), not by height, so y=5px on
        // a 30-wide image is 5/30 here, not 5/10.
        paint.stroke.append(BrushStrokePoint{QPointF(2.0 / 30.0, 5.0 / 30.0), false,
                                             paint.brushRadius, paint.hardness,
                                             paint.paintColor.rgb()});

        Mask shape;
        shape.type = MaskType::Shape;
        shape.shapeType = ShapeType::Rectangle;
        shape.shapeRect = QRectF(12, 2, 6, 6);
        shape.shapeFillEnabled = true;
        shape.shapeFillColor = QColor(255, 0, 0);
        shape.shapeStrokeEnabled = false;
        shape.opacity = 1.0;

        Mask tb;
        tb.type = MaskType::TextBox;
        tb.textBoxPos = QPointF(22, 0);
        tb.textBoxText = QStringLiteral("X");
        tb.textBoxBgEnabled = true;
        tb.textBoxBgColor = QColor(0, 255, 0);
        tb.textBoxBgOpacity = 1.0;
        tb.textBoxBgPadding = 3.0;
        tb.opacity = 1.0;

        Adjustments adj;
        // masks are top-of-stack-first; order here doesn't matter for this
        // test since the three regions don't overlap.
        adj.masks.append(tb);
        adj.masks.append(shape);
        adj.masks.append(paint);

        QImage out = applyAdjustments(base, adj);
        applyPaintMasks(out, adj.masks);
        assert(qBlue(out.pixel(2, 5)) > 200);  // Paint region
        assert(qRed(out.pixel(15, 5)) > 200);  // Shape region
        assert(qGreen(out.pixel(23, 1)) > 200); // TextBox background region
    }

    // hasMaskEdits/hasToneEdits recognize Shape/TextBox masks.
    {
        Adjustments a;
        Mask shape;
        shape.type = MaskType::Shape;
        shape.shapeRect = QRectF(0, 0, 4, 4);
        a.masks.append(shape);
        assert(hasToneEdits(a));

        Adjustments b;
        Mask tbEmpty;
        tbEmpty.type = MaskType::TextBox;
        tbEmpty.textBoxText = QStringLiteral("   "); // whitespace-only -> no edit
        b.masks.append(tbEmpty);
        assert(!hasToneEdits(b));
    }

    // MaskType::Background: the tab's base photo, represented as a normal
    // Mask entry (see RetouchTab::ensureBackgroundMask), goes through the
    // exact same generic compositing/visibility/z-order code path as every
    // other layer in applyMasks — no pinned bottom slot, no special hide/
    // delete flag. This is the core regression coverage for that migration.
    {
        QImage base(4, 4, QImage::Format_ARGB32);
        base.fill(QColor(0, 0, 255)); // blue

        Mask bg;
        bg.type = MaskType::Background;
        bg.name = QStringLiteral("Background");

        Mask paint;
        paint.type = MaskType::Paint;
        paint.paintColor = QColor(255, 0, 0); // red
        paint.brushRadius = 2.0;
        paint.hardness = 1.0;
        paint.opacity = 1.0;
        paint.blend = BlendMode::Normal;
        paint.stroke.append(BrushStrokePoint{QPointF(0.5, 0.5), false, paint.brushRadius,
                                             paint.hardness, paint.paintColor.rgb()});

        // Standard order: Paint above Background (index 0 = top of stack) ->
        // the paint layer wins.
        {
            Adjustments adj;
            adj.masks.append(paint);
            adj.masks.append(bg);
            QImage out = applyAdjustments(base, adj);
            applyPaintMasks(out, adj.masks);
            QRgb center = out.pixel(2, 2);
            assert(qRed(center) > 250 && qGreen(center) < 5 && qBlue(center) < 5);
        }

        // Reordered: Background dragged above another full-frame (static-
        // tier) layer -> Background is fully opaque and composites last, so
        // it now hides that layer entirely. Proves Background is genuinely
        // reorderable, not pinned to the bottom of the stack. (Uses an image
        // layer rather than Paint here: Paint/Shape/TextBox are the
        // "interactive tier", always composited as a block on top of the
        // static tier regardless of stack order — a pre-existing, documented
        // rendering-pipeline trade-off unrelated to Background specifically,
        // see the MaskPass comment in Adjustments.cpp.)
        {
            QImage red(4, 4, QImage::Format_ARGB32);
            red.fill(QColor(255, 0, 0));
            Mask imageLayer;
            imageLayer.type = MaskType::None;
            imageLayer.sourceImagePath = QStringLiteral("layer.png");
            imageLayer.sourceImageCache = red;

            Adjustments adj;
            adj.masks.append(bg);
            adj.masks.append(imageLayer);
            QImage out = applyAdjustments(base, adj);
            QRgb center = out.pixel(2, 2);
            assert(qBlue(center) > 250 && qRed(center) < 5);
        }

        // Hiding Background (Mask::visible = false, the same generic flag
        // every other layer uses) renders it as transparent, same as any
        // other hidden full-frame layer -- no separate backgroundHidden flag.
        {
            Adjustments adj;
            Mask hiddenBg = bg;
            hiddenBg.visible = false;
            adj.masks.append(hiddenBg);
            QImage out = applyAdjustments(base, adj);
            assert(qAlpha(out.pixel(2, 2)) == 0);
        }

        // A default (untouched) Background entry alone must not itself count
        // as an "edit" -- otherwise every freshly-opened image would show as
        // dirty/edited purely because Background always exists in masks[].
        {
            Adjustments adj;
            adj.masks.append(bg);
            assert(!hasToneEdits(adj));
        }

        // But a Background layer with a real per-layer adjustment (or one
        // that's hidden) does count, same as any other mask.
        {
            Adjustments adj;
            Mask editedBg = bg;
            editedBg.adj.brightness = 20;
            adj.masks.append(editedBg);
            assert(hasToneEdits(adj));
        }
    }

    std::printf("AdjustmentsPaintTest: all assertions passed\n");
    return 0;
}
