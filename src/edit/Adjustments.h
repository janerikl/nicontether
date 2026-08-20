#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>
#include <QMetaType>
#include <QTransform>
#include <QPainterPath>
#include <cmath>
#include <vector>

// One channel of a Photoshop-style Levels adjustment: input black/white points
// clip and stretch the tonal range, gamma remaps the midtones, and the output
// range compresses the result. Black/white points are stored in legacy 0-255
// units (rescaled internally to the 16-bit working range) — see
// buildLevelsLut — a constrained tone curve, applied composite-then-per-channel.
struct LevelsChannel {
    int inBlack = 0;     // 0..255 — input shadow clip
    int inWhite = 255;   // 0..255 — input highlight clip
    double gamma = 1.0;  // 0.1..9.99 — midtone; >1 brightens, <1 darkens
    int outBlack = 0;    // 0..255 — output shadow floor
    int outWhite = 255;  // 0..255 — output highlight ceiling

    bool isIdentity() const {
        return inBlack == 0 && inWhite == 255 && std::abs(gamma - 1.0) < 1e-4 &&
               outBlack == 0 && outWhite == 255;
    }
    bool operator==(const LevelsChannel &o) const {
        return inBlack == o.inBlack && inWhite == o.inWhite &&
               std::abs(gamma - o.gamma) < 1e-9 && outBlack == o.outBlack &&
               outWhite == o.outWhite;
    }
    bool operator!=(const LevelsChannel &o) const { return !(*this == o); }
};

// Full Levels: a composite (rgb) channel applied to all three channels, plus
// independent per-channel adjustments for colour correction.
struct Levels {
    LevelsChannel rgb; // composite, applied to R, G and B
    LevelsChannel r;
    LevelsChannel g;
    LevelsChannel b;

    bool isIdentity() const {
        return rgb.isIdentity() && r.isIdentity() && g.isIdentity() &&
               b.isIdentity();
    }
    bool operator==(const Levels &o) const {
        return rgb == o.rgb && r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const Levels &o) const { return !(*this == o); }
};

// One targeted color-range adjustment (Levels panel eyedropper tool): the
// user clicks a spot on the image to sample a target color, then drags
// horizontally; `amount` shifts the level of the sampled color's dominant
// channel for all pixels colour-similar to the target (fixed tolerance,
// smooth falloff — see applyTone in Adjustments.cpp).
struct ColorRangeAdjust {
    int r = 0, g = 0, b = 0;   // sampled target color, 0-255
    int channel = 0;           // 0=R, 1=G, 2=B — dominant channel of the pick
    int amount = 0;            // -100..100, drag-accumulated delta

    bool operator==(const ColorRangeAdjust &o) const {
        return r == o.r && g == o.g && b == o.b && channel == o.channel &&
               amount == o.amount;
    }
    bool operator!=(const ColorRangeAdjust &o) const { return !(*this == o); }
};

// The subset of tone/colour adjustments a local mask can apply. These blend
// cleanly per-pixel (weighted by the mask), unlike clarity/sharpen/vignette
// (neighbourhood/position dependent) or curve/levels.
struct MaskAdjust {
    int brightness = 0;
    int contrast = 0;
    int highlights = 0;
    int shadows = 0;
    int saturation = 0;
    int vibrance = 0;
    int temperature = 0;
    int tint = 0;
    double wbR = 1.0;
    double wbG = 1.0;
    double wbB = 1.0;

    // Detail / effects — same meaning as the global fields, applied within
    // this layer only (see applyLayerContent in Adjustments.cpp).
    int denoise = 0;
    int clarity = 0;
    int sharpen = 0;
    int vignette = 0;

    // Artificial directional lighting: fakes a light source raking across a
    // surface estimated from the image's own luminance (see applyLighting in
    // Adjustments.cpp). `lightAngle` (0..360°) only matters when
    // `lightIntensity` (-100..100, 0 = off) is non-zero.
    int lightAngle = 0;
    int lightIntensity = 0;

    // Tone curve + Levels, same semantics as the global fields.
    QVector<QPointF> curve;
    Levels levels;

    bool hasCurve() const {
        if (curve.size() < 2) return false;
        for (const QPointF &p : curve)
            if (std::abs(p.x() - p.y()) > 1e-4) return true;
        return false;
    }
    bool isZero() const {
        return !brightness && !contrast && !highlights && !shadows &&
               !saturation && !vibrance && !temperature && !tint &&
               std::abs(wbR - 1.0) < 1e-4 && std::abs(wbG - 1.0) < 1e-4 &&
               std::abs(wbB - 1.0) < 1e-4 && !denoise && !clarity && !sharpen &&
               !vignette && !lightIntensity && !hasCurve() && levels.isIdentity();
    }
    bool operator==(const MaskAdjust &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance &&
               temperature == o.temperature && tint == o.tint &&
               wbR == o.wbR && wbG == o.wbG && wbB == o.wbB &&
               denoise == o.denoise && clarity == o.clarity &&
               sharpen == o.sharpen && vignette == o.vignette &&
               lightAngle == o.lightAngle && lightIntensity == o.lightIntensity &&
               curve == o.curve && levels == o.levels;
    }
    bool operator!=(const MaskAdjust &o) const { return !(*this == o); }
};

// Background: the tab's own loaded base photo, represented as a normal Mask
// entry so it goes through the exact same visibility/delete/reorder/thumbnail
// code paths as every other layer (see RetouchTab::ensureBackgroundMask).
// Full-frame, non-repositionable content sourced from `sourceImageCache`
// (populated by RetouchTab from its base image, never from an external file
// — `sourceImagePath` stays empty so isImageLayer() does not also match it).
enum class MaskType { Radial, Linear, Brush, Paint, Text, None, Shape, TextBox, Background };

enum class ShapeType { Rectangle, Ellipse, Line, Polygon, Star, Heart };

// How a layer's local adjustment composites over what's below it. Applied
// per-channel in sRGB space, then mixed with the layer below by mask weight
// x opacity (see blendChannel in Adjustments.cpp).
// Hue/Saturation/Color/Luminosity aren't per-channel-independent (they act on
// the layer's/backdrop's full RGB triple via the standard HSL swap formulas),
// so they're handled by blendHSLTriple() rather than blendChannel() — see
// Adjustments.cpp's composite loop for the branch.
enum class BlendMode { Normal, Multiply, Screen, Overlay, SoftLight,
                       Darken, Lighten, ColorDodge, ColorBurn, Difference, Exclusion,
                       Hue, Saturation, Color, Luminosity };

// One sampled point of a brush stroke (width-normalized). `erase` marks a dab
// painted while holding Alt, which subtracts coverage instead of adding it.
// `radius`/`hardness`/`color` are captured from the mask's brush settings at
// the moment the dab was painted, so later changes to brush size, hardness,
// or (for a Paint-type mask) fill color only affect new dabs, not ones
// already committed to the stroke. `color` is unused for MaskType::Brush.
struct BrushStrokePoint {
    QPointF pt;
    bool erase = false;
    double radius = 0.06;
    double hardness = 0.5;
    QRgb color = 0xFF000000;
    // True for the first sample of a mouse-down drag. Rasterization only
    // interpolates sub-dabs between this point and its predecessor when this
    // is false, so separate strokes never get an unwanted connecting line.
    bool newStroke = false;
    // Stylus pressure at the moment this dab was painted (0..1). Defaults to
    // 1.0 for mouse input; only real QTabletEvent::pressure() varies it.
    // Used by pen-style dabs to modulate per-dab radius (see rasterizeBrush).
    qreal pressure = 1.0;
    // True when this dab was painted with the Pen tool active, rather than
    // Brush — the two tools share the same Paint-type layer/stroke so a user
    // can freely switch between them without leaving the layer, and each dab
    // remembers which rendering treatment it wants. `penGrade` is only
    // meaningful when `isPen` is true; like radius/hardness it's captured
    // from the mask's pen-grade setting at the moment the dab was painted.
    bool isPen = false;
    double penGrade = 0.0; // -6.0(6B)..5.0(5H), see rasterizeBrush's penParams

    // Clone-stamp dab: instead of filling with a flat `color`, rasterizeBrush
    // samples per-pixel from the composite-so-far reference image (`ref`),
    // offset by (cloneSourcePt - pt) — so the dab reproduces the source's
    // actual texture, not a single averaged color. `color` is unused when
    // this is true.
    bool isClone = false;
    QPointF cloneSourcePt; // width-normalized, same convention as pt

    bool operator==(const BrushStrokePoint &o) const {
        return pt == o.pt && erase == o.erase && color == o.color &&
               newStroke == o.newStroke && isPen == o.isPen && isClone == o.isClone &&
               cloneSourcePt == o.cloneSourcePt &&
               std::abs(radius - o.radius) < 1e-9 &&
               std::abs(hardness - o.hardness) < 1e-9 &&
               std::abs(pressure - o.pressure) < 1e-9 &&
               std::abs(penGrade - o.penGrade) < 1e-9;
    }
};

// One dab of an erase-tool stroke on an image layer: canvas-width-normalized
// centre + radius (same normalization as BrushStrokePoint / m.brushRadius).
// Dabs are max-combined into a coverage buffer, then subtracted from the
// layer's alpha at composite time (see applyMasks in Adjustments.cpp).
// Always soft/feathered across the full radius — no hard-edge option.
struct ErasePoint {
    QPointF pt;
    double radius = 0.06;

    bool operator==(const ErasePoint &o) const {
        return pt == o.pt && std::abs(radius - o.radius) < 1e-9;
    }
};

// A single spot-heal: a circular region (in oriented-image coordinates, i.e.
// after rotation/flip but before crop) that gets replaced with a nearby patch.
struct HealOp {
    int x = 0;
    int y = 0;
    int radius = 0;
    bool operator==(const HealOp &o) const {
        return x == o.x && y == o.y && radius == o.radius;
    }
};

// One adjustment layer in the stack. All geometry is stored normalized to the
// image WIDTH (x' = x/W, y' = y/W) so it is resolution-independent and scales
// uniformly between the display preview and full-res export. Applied after
// the global (base layer) tone pass, in cropped-oriented image space, in
// stack order. `type == MaskType::None` is an unmasked layer — its adjustment
// applies to the whole frame (geometry fields are unused).
struct Mask {
    QString name;
    bool visible = true;
    double opacity = 1.0;      // 0..1, on top of the mask's own weight
    BlendMode blend = BlendMode::Normal;
    // Display name of the group this layer belongs to (mirrored across every
    // member of the group; only meaningful when groupId is non-empty).
    QString groupName;

    // Layers sharing a non-empty groupId belong to the same group (assigned
    // by RetouchTab::groupMasks, cleared by ungroupMasks) and are always kept
    // contiguous in the stack, mirroring ShapeOp::groupId.
    QString groupId;

    MaskType type = MaskType::Radial;
    bool inverted = false;
    double feather = 0.5; // 0..1 fraction of the radius/edge that fades

    // Radial: centre + radii (width-normalized) and rotation.
    QPointF center{0.5, 0.5};
    double radiusX = 0.25;
    double radiusY = 0.25;
    double angle = 0.0; // radians

    // Linear (graduated): full effect at p0, fading to zero at p1.
    QPointF p0{0.5, 0.2};
    QPointF p1{0.5, 0.6};

    // Brush: stroke points (width-normalized), radius, and edge hardness.
    QVector<BrushStrokePoint> stroke;
    double brushRadius = 0.06;
    double hardness = 0.5;
    // Auto Mask (brush only): constrains each dab to pixels colour-similar to
    // the point under the cursor, so the stroke stops at edges (Lightroom-style).
    bool autoMask = false;

    // Pen tool: current pencil "grade" setting, baked into each new dab's
    // BrushStrokePoint::penGrade at paint time (same pattern as brushRadius/
    // hardness above) whenever the Pen tool — not Brush — is active on this
    // Paint-type layer. Drives per-dab hardness/opacity/grain/pressure-
    // sensitivity, following real pencil grade convention: -6.0 = "6B"
    // (softest, smudgy, heavy) .. 0.0 = "HB" .. +5.0 = "5H" (hardest, crisp,
    // light). See rasterizeBrush's penParams in Adjustments.cpp.
    double penGrade = 0.0;

    // Paint: flat fill color for a MaskType::Paint layer. Composited using
    // the same `stroke`/`brushRadius`/`hardness` coverage as MaskType::Brush,
    // but the layer's content is a solid fill of this color instead of a
    // tone-adjusted copy of the image below (see applyMasks in
    // Adjustments.cpp). Unused by all other mask types.
    QColor paintColor = Qt::black;

    // Gradient Fill: when true, a Radial/Linear mask's own content is a flat
    // two-color interpolation (see renderGradientFill in Adjustments.cpp)
    // instead of a tone-adjusted copy of the image below — `gradientColorA`
    // shows at the mask's center/start point, `gradientColorB` at/beyond its
    // far edge. Unused when false (the mask behaves as an ordinary Radial/
    // Linear adjustment mask, using `adj` as normal).
    bool isGradientFill = false;
    QColor gradientColorA = Qt::white;
    QColor gradientColorB = Qt::black;

    // Paint bucket: cumulative flood-filled regions, composited alongside
    // `stroke` coverage for MaskType::Paint (see applyMasks in
    // Adjustments.cpp). ARGB32, resolution-independent the same way
    // `sourceImageCache` scaling is — stored at whatever resolution the fill
    // was computed at and resampled to the current render buffer's size at
    // composite time, since it always covers the same normalized unit square
    // as `stroke`. Empty/null when nothing has been bucket-filled yet.
    QImage fillMask;

    // Text: knockout/clipping-text coverage — the layer's content (whatever
    // is below it, tone-adjusted by `adj` like a plain layer) only shows
    // through where these glyphs are. Plain shape only, no fill/outline/
    // shadow of its own, since the coverage itself carries no colour.
    // `textPos`/`textPixelSize` are width-normalized (same convention as
    // `center`/`radiusX`), so they scale with the image like other mask
    // geometry. Unused by all other mask types.
    QString text;
    QString textFamily = QStringLiteral("Sans Serif");
    double textPixelSize = 0.08; // width-normalized (fraction of image width)
    bool textBold = false;
    bool textItalic = false;
    QPointF textPos{0.3, 0.45}; // top-left, width-normalized

    // Shape (MaskType::Shape): a real shape layer (rectangle/ellipse/line/
    // polygon/star/heart), mirroring ShapeOp's fields. `opacity`/`visible`/
    // `groupId`/`name` above are reused for what ShapeOp calls
    // opacity/visible/groupId (ShapeOp has no name; new Shape masks get an
    // auto-generated name at creation time in a later session).
    // TODO(shape-layer migration stage B): shapeRect/shapeP1/shapeP2 are
    // currently in raw oriented-image pixel space, pre-crop, like legacy
    // ShapeOp — NOT width-normalized like other Mask geometry (center/p0/p1
    // above). Converting to the normalized convention is deferred to the
    // rendering-pipeline session since it requires rewriting ShapeTool.cpp's
    // geometry math and canvas hit-testing together.
    ShapeType shapeType = ShapeType::Rectangle;
    QRectF shapeRect{0, 0, 200, 200};
    QPointF shapeP1{0, 0};
    QPointF shapeP2{200, 0};
    double shapeRotation = 0.0; // degrees, clockwise
    int shapeSides = 5;
    double shapeInnerRadiusRatio = 0.5;
    bool shapeFillEnabled = true;
    QColor shapeFillColor{255, 255, 255, 255};
    bool shapeStrokeEnabled = true;
    QColor shapeStrokeColor{0, 0, 0, 255};
    double shapeStrokeWidth = 4.0;

    // Asset-stamp image fill: when set, this Shape mask is filled with a
    // stored cutout image (scaled to fit shapeRect, clipped to the shape
    // path, rotated by shapeRotation) instead of shapeFillColor -- reusing
    // Shape's existing move/resize/rotate handles for placed asset stamps.
    // shapeImageCache is a transient decode cache like sourceImageCache
    // above: never serialized, never compared.
    QString shapeImagePath;
    QImage shapeImageCache;
    bool isShapeImageFilled() const { return !shapeImagePath.isEmpty(); }

    // TextBox (MaskType::TextBox): a real text layer, mirroring TextOp's
    // fields (distinct from the Text clip-mask fields above, which carry no
    // colour/outline/shadow/background of their own). Same deferred raw
    // oriented-image pixel-space convention as TextOp::pos for now (see
    // TODO(shape-layer migration stage B) note above).
    QPointF textBoxPos{0, 0};
    double textBoxRotation = 0.0; // degrees, clockwise, about textBoxPos
    QString textBoxText;
    QString textBoxFamily = QStringLiteral("Sans Serif");
    double textBoxPixelSize = 48.0;
    bool textBoxBold = false;
    bool textBoxItalic = false;
    QColor textBoxColor{255, 255, 255, 255};
    bool textBoxOutlineEnabled = false;
    QColor textBoxOutlineColor{0, 0, 0, 255};
    double textBoxOutlineWidth = 3.0;
    bool textBoxShadowEnabled = false;
    QPointF textBoxShadowOffset{8, 8};
    double textBoxShadowBlur = 14.0;
    double textBoxShadowOpacity = 0.75;
    QColor textBoxShadowColor{0, 0, 0, 255};
    bool textBoxBgEnabled = false;
    QColor textBoxBgColor{0, 0, 0, 255};
    double textBoxBgOpacity = 0.6;
    double textBoxBgPadding = 10.0;

    MaskAdjust adj;

    // Image layer: when set, this layer's content is a cover-fit scale/crop of
    // the referenced photo (tone/curve/levels/detail from `adj` still applies
    // to it) instead of the composite below. `sourceImageOffset` pans the
    // fitted source photo, `sourceImageScale` resizes it relative to the full
    // frame, and `sourceImageLockRatio` keeps the resize uniform when on.
    // `sourceImageCache`/`sourceMissing` are a transient decode cache populated
    // only by RetouchTab — never serialized, never compared — so this stays
    // cheap to copy for undo snapshots and doesn't cause spurious history
    // entries.
    QString sourceImagePath;
    QPointF sourceImageOffset{0.0, 0.0};
    QPointF sourceImageScale{1.0, 1.0};
    bool sourceImageLockRatio = true;
    QImage sourceImageCache;
    bool sourceMissing = false;
    bool isImageLayer() const { return !sourceImagePath.isEmpty(); }
    // Background layer: content is the tab's own loaded base photo (see
    // MaskType::Background comment above), sourced via `sourceImageCache`
    // like an image layer but never from an external file/path.
    bool isBackgroundLayer() const { return type == MaskType::Background; }

    // Erase-tool strokes: canvas-normalized dabs that punch a feathered
    // reduction into this layer's final compositing weight at render time.
    // Works on any layer type (image, background, paint, brush, shape,
    // text, text box, or an adjustment mask).
    QVector<ErasePoint> eraseStrokes;

    // Spot-heal ops owned by this layer, in the same oriented-image,
    // pre-crop coordinate space as Adjustments::heals — but applied to this
    // layer's own pixel content (image/background layers only; see
    // applyMasks in Adjustments.cpp) instead of the tab's base image, so
    // healing a duplicated/independent layer doesn't touch the original.
    QVector<HealOp> heals;

    // Active-selection clip (width-normalized), baked in by RetouchTab
    // whenever a marquee/lasso/magic-wand selection is active while this
    // layer is being painted/erased — new pixels only land inside it, tested
    // per-pixel at rasterization time (see rasterizeBrush/applyMasks in
    // Adjustments.cpp), not just gated at each dab's center point. Transient
    // UI state like sourceImageCache above: never serialized, never compared.
    QPainterPath selectionClipNorm;
    // Feather (Photoshop's Select > Feather): softens selectionClipNorm's
    // edge over this many width-normalized units instead of a hard cutoff.
    // Transient like selectionClipNorm above: never serialized, never compared.
    double selectionFeatherNorm = 0.0;

    bool operator==(const Mask &o) const {
        return name == o.name && visible == o.visible && groupId == o.groupId &&
               groupName == o.groupName &&
               std::abs(opacity - o.opacity) < 1e-9 && blend == o.blend &&
               type == o.type && inverted == o.inverted &&
               std::abs(feather - o.feather) < 1e-9 && center == o.center &&
               std::abs(radiusX - o.radiusX) < 1e-9 &&
               std::abs(radiusY - o.radiusY) < 1e-9 &&
               std::abs(angle - o.angle) < 1e-9 && p0 == o.p0 && p1 == o.p1 &&
               stroke == o.stroke && eraseStrokes == o.eraseStrokes &&
               heals == o.heals &&
               std::abs(brushRadius - o.brushRadius) < 1e-9 &&
               std::abs(hardness - o.hardness) < 1e-9 && autoMask == o.autoMask &&
               std::abs(penGrade - o.penGrade) < 1e-9 &&
               adj == o.adj && paintColor == o.paintColor && fillMask == o.fillMask &&
               text == o.text && textFamily == o.textFamily &&
               std::abs(textPixelSize - o.textPixelSize) < 1e-9 &&
               textBold == o.textBold && textItalic == o.textItalic &&
               textPos == o.textPos &&
               sourceImageOffset == o.sourceImageOffset &&
               sourceImageScale == o.sourceImageScale &&
               sourceImageLockRatio == o.sourceImageLockRatio &&
               sourceImagePath == o.sourceImagePath &&
               shapeType == o.shapeType && shapeRect == o.shapeRect &&
               shapeP1 == o.shapeP1 && shapeP2 == o.shapeP2 &&
               std::abs(shapeRotation - o.shapeRotation) < 1e-9 &&
               shapeSides == o.shapeSides &&
               std::abs(shapeInnerRadiusRatio - o.shapeInnerRadiusRatio) < 1e-9 &&
               shapeFillEnabled == o.shapeFillEnabled &&
               shapeFillColor == o.shapeFillColor &&
               shapeStrokeEnabled == o.shapeStrokeEnabled &&
               shapeStrokeColor == o.shapeStrokeColor &&
               std::abs(shapeStrokeWidth - o.shapeStrokeWidth) < 1e-9 &&
               shapeImagePath == o.shapeImagePath &&
               textBoxPos == o.textBoxPos &&
               std::abs(textBoxRotation - o.textBoxRotation) < 1e-9 &&
               textBoxText == o.textBoxText && textBoxFamily == o.textBoxFamily &&
               std::abs(textBoxPixelSize - o.textBoxPixelSize) < 1e-9 &&
               textBoxBold == o.textBoxBold && textBoxItalic == o.textBoxItalic &&
               textBoxColor == o.textBoxColor &&
               textBoxOutlineEnabled == o.textBoxOutlineEnabled &&
               textBoxOutlineColor == o.textBoxOutlineColor &&
               std::abs(textBoxOutlineWidth - o.textBoxOutlineWidth) < 1e-9 &&
               textBoxShadowEnabled == o.textBoxShadowEnabled &&
               textBoxShadowOffset == o.textBoxShadowOffset &&
               std::abs(textBoxShadowBlur - o.textBoxShadowBlur) < 1e-9 &&
               std::abs(textBoxShadowOpacity - o.textBoxShadowOpacity) < 1e-9 &&
               textBoxShadowColor == o.textBoxShadowColor &&
               textBoxBgEnabled == o.textBoxBgEnabled &&
               textBoxBgColor == o.textBoxBgColor &&
               std::abs(textBoxBgOpacity - o.textBoxBgOpacity) < 1e-9 &&
               std::abs(textBoxBgPadding - o.textBoxBgPadding) < 1e-9;
    }
    bool operator!=(const Mask &o) const { return !(*this == o); }
};

// Incremental-rasterization cache for one brush/paint mask's stroke coverage.
// Repainting a stroke re-sends the *whole* point list every time, so without
// this a live drag would re-rasterize all points from scratch on every move
// (O(n^2) over a stroke). Callers that render repeatedly for the same
// mask/resolution across a drag (RenderWorker, ImageCanvas) keep one of
// these around and pass it in; only newly-appended points get rasterized,
// and the sentinel fields detect when the cache no longer applies (stroke
// shrank, brush params changed, different resolution) and fall back to a
// full rebuild. Deliberately not part of Mask/Adjustments so it is never
// copied along with undo snapshots or render-queue copies.
struct BrushRasterCache {
    std::vector<uchar> cov;
    std::vector<QRgb> col; // per-pixel dab color, only populated for Paint-type masks
    // Cumulative erase-dab strength, independent of `cov` — Paint-type masks
    // use this to also attenuate bucket-fill coverage (see rasterizeBrush),
    // since an erase dab must cut through fill as well as stroke coverage.
    std::vector<uchar> erase;
    int w = 0, h = 0;
    int pointCount = 0;
    double brushRadius = -1;
    double hardness = -1;
    bool autoMask = false;
    BrushStrokePoint lastPoint;
    bool valid = false;
    // Drag-preview dirty-rect fast path (see applyMasks' paintLayer branch):
    // the fully-composited `img` this mask produced last drag frame, so the
    // next frame can patch in just the newest dab's bounding box instead of
    // recompositing the whole buffer. Only valid when the mask has no erase
    // strokes this frame (those aren't append-only, so a partial patch could
    // miss pixels outside the new dab's rect) - see paintDirtyRectEligible in
    // Adjustments.cpp.
    QImage lastComposite;
    bool lastCompositeValid = false;
    // A bucket-fill/Ctrl+Backspace `fillMask` is static across a drag (only a
    // fresh fill or bucket-fill click replaces it), so the fast path can
    // still apply with one present — it just needs this frame's per-pixel
    // fill alpha/color to fold into the touched-rect patch the same way the
    // full-buffer path does. Cached here (already scaled to this mask's w/h)
    // so a drag frame doesn't have to rescale the fill image every time;
    // `fillMaskCacheKey` (QImage::cacheKey(), which changes iff the pixel
    // data changes) detects a fill actually changing since the last full
    // recompute primed this cache, without needing a full pixel compare.
    QImage fillScaledCache;
    qint64 fillMaskCacheKey = 0;
    bool fillScaledValid = false;
};

// One text overlay. `pos` is in oriented-image pixel space, pre-crop (same
// convention as HealOp), so text stays anchored to photo content across crop
// changes. Font/outline/shadow metrics are absolute image-space pixels, same
// scaling convention as HealOp::radius. Composited as the very last step of
// rendering (after tone/colour/vignette), so — unlike heals — text pixels are
// never touched by tone/colour adjustments (see applyTexts in TextTool.cpp,
// called from RetouchTab after the toned render, not from applyAdjustments).
struct TextOp {
    QPointF pos{0, 0};
    double rotation = 0.0; // degrees, clockwise, about pos

    QString text;          // may contain '\n' for multiple lines

    QString family = QStringLiteral("Sans Serif");
    double pixelSize = 48.0;
    bool bold = false;
    bool italic = false;

    QColor color{0, 0, 0, 255};

    bool outlineEnabled = false;
    QColor outlineColor{0, 0, 0, 255};
    double outlineWidth = 3.0;

    bool shadowEnabled = false;
    QPointF shadowOffset{8, 8};
    double shadowBlur = 14.0;
    double shadowOpacity = 0.75;
    QColor shadowColor{0, 0, 0, 255};

    // Background: a solid box drawn behind the text (banner/highlight look).
    // Drawn first, before shadow/fill/outline, sized to the text bounds plus
    // `bgPadding` (image-space px) on all sides.
    bool bgEnabled = false;
    QColor bgColor{0, 0, 0, 255};
    double bgOpacity = 0.6;
    double bgPadding = 10.0;

    bool operator==(const TextOp &o) const {
        return pos == o.pos && std::abs(rotation - o.rotation) < 1e-9 &&
               text == o.text && family == o.family &&
               std::abs(pixelSize - o.pixelSize) < 1e-9 && bold == o.bold &&
               italic == o.italic && color == o.color &&
               outlineEnabled == o.outlineEnabled &&
               outlineColor == o.outlineColor &&
               std::abs(outlineWidth - o.outlineWidth) < 1e-9 &&
               shadowEnabled == o.shadowEnabled &&
               shadowOffset == o.shadowOffset &&
               std::abs(shadowBlur - o.shadowBlur) < 1e-9 &&
               std::abs(shadowOpacity - o.shadowOpacity) < 1e-9 &&
               shadowColor == o.shadowColor &&
               bgEnabled == o.bgEnabled && bgColor == o.bgColor &&
               std::abs(bgOpacity - o.bgOpacity) < 1e-9 &&
               std::abs(bgPadding - o.bgPadding) < 1e-9;
    }
    bool operator!=(const TextOp &o) const { return !(*this == o); }
};

// One shape overlay (rectangle/ellipse/line/polygon/star/heart). `rect` is
// the bounding box in oriented-image pixel space, pre-crop (same convention
// as TextOp::pos); `p1`/`p2` are used instead for Line. Composited as the
// very last step of rendering, right after texts (see applyShapes in
// ShapeTool.cpp, called from RetouchTab after the toned render), so shape
// pixels are never touched by tone/colour adjustments.
struct ShapeOp {
    ShapeType type = ShapeType::Rectangle;

    QRectF rect{0, 0, 200, 200}; // Rectangle/Ellipse/Polygon/Star/Heart
    QPointF p1{0, 0};             // Line start
    QPointF p2{200, 0};           // Line end

    double rotation = 0.0; // degrees, clockwise, about rect.center() (or p1/p2 midpoint for Line)

    int sides = 5;                  // Polygon/Star point count (3..20)
    double innerRadiusRatio = 0.5;  // Star only (0.1..0.9)

    bool fillEnabled = true;
    QColor fillColor{255, 255, 255, 255};
    // Asset-stamp image fill: when non-null, the shape is filled by drawing
    // this image scaled to `rect` (clipped to the shape path) instead of
    // `fillColor`. Not owned, never compared/serialized -- callers (see
    // Adjustments.cpp's rasterizeShapeOrTextBox) point it at a decode cache
    // that outlives the call.
    const QImage *fillImage = nullptr;
    bool strokeEnabled = true;
    QColor strokeColor{0, 0, 0, 255};
    double strokeWidth = 4.0;

    double opacity = 1.0; // 0..1, applied to whole shape (fill+stroke)

    bool visible = true; // Layers-panel eye toggle; hidden shapes are skipped by applyShapes
    // Shapes sharing a non-empty groupId belong to the same group (assigned
    // by RetouchTab::groupSelectedShapes, cleared by ungroupSelectedShapes).
    // Grouped shapes are kept contiguous in the stack and select/move/resize
    // together; empty means ungrouped. Not a QUuid field to keep EditSidecar
    // (de)serialization a plain string round-trip.
    QString groupId;

    bool operator==(const ShapeOp &o) const {
        return type == o.type && rect == o.rect && p1 == o.p1 && p2 == o.p2 &&
               std::abs(rotation - o.rotation) < 1e-9 && sides == o.sides &&
               std::abs(innerRadiusRatio - o.innerRadiusRatio) < 1e-9 &&
               fillEnabled == o.fillEnabled && fillColor == o.fillColor &&
               strokeEnabled == o.strokeEnabled && strokeColor == o.strokeColor &&
               std::abs(strokeWidth - o.strokeWidth) < 1e-9 &&
               std::abs(opacity - o.opacity) < 1e-9 &&
               visible == o.visible && groupId == o.groupId;
    }
    bool operator!=(const ShapeOp &o) const { return !(*this == o); }
};

// A content-aware "remove object" region: the user paints a brush stroke
// over an unwanted object; on release, RetouchTab runs InpaintTool::inpaint
// once and caches the result here so it never needs recomputing on repaint
// (unlike heals, this doesn't re-derive the fill from the image each time —
// the sampled/synthesized pixels are baked into `fill` at stroke-release
// time). `rect`/`mask`/`fill` are in oriented-image coordinates, pre-crop
// (same convention as HealOp/ShapeOp). `mask` and `fill` are both sized to
// `rect`: `mask` marks which pixels of `rect` are part of the removed
// region (alpha>0 = filled), `fill` holds the corresponding replacement
// pixels. `visible` is the Layers-panel eye toggle, mirroring ShapeOp.
struct RemoveObjectOp {
    QVector<QPointF> stroke; // brush-stroke centreline, oriented-image coords (for redisplay/hit-testing)
    double radius = 20.0;    // brush radius, oriented-image pixels
    QRect rect;              // bounding box of mask/fill, oriented-image coords, pre-crop
    QImage mask;             // same size as rect; alpha>0 marks filled pixels
    QImage fill;             // same size as rect; cached inpainted result

    bool visible = true;

    bool operator==(const RemoveObjectOp &o) const {
        return stroke == o.stroke && std::abs(radius - o.radius) < 1e-9 &&
               rect == o.rect && mask == o.mask && fill == o.fill &&
               visible == o.visible;
    }
    bool operator!=(const RemoveObjectOp &o) const { return !(*this == o); }
};

// Persisted state for a layer group, identified by `id` matching the
// `groupId` string shared by its member Masks. Group membership/order is
// still entirely determined by which masks share `id` and their contiguous
// position in Adjustments::masks (see Mask::groupId) — this struct only
// carries the group's OWN properties, which apply on top of its members like
// a single virtual layer wrapping them (see applyMasks in Adjustments.cpp).
struct MaskGroup {
    QString id;
    QString name;
    double opacity = 1.0;
    bool visible = true;
    BlendMode blend = BlendMode::Normal;
    bool collapsed = false; // UI expand/collapse state, persisted so it survives a reload

    bool operator==(const MaskGroup &o) const {
        return id == o.id && name == o.name && visible == o.visible && blend == o.blend &&
               collapsed == o.collapsed && std::abs(opacity - o.opacity) < 1e-9;
    }
    bool operator!=(const MaskGroup &o) const { return !(*this == o); }
};

// Non-destructive edit parameters applied on top of an immutable base image.
// Unless noted, sliders are in [-100, 100] with 0 = no change.
struct Adjustments {
    // Tone
    int brightness = 0;      // additive lightness
    int contrast = 0;        // contrast expansion around mid-grey
    int highlights = 0;      // recover (-) / boost (+) bright regions
    int shadows = 0;         // lift (+) / deepen (-) dark regions

    // Colour
    int saturation = 0;      // global saturation
    int vibrance = 0;        // saturation weighted to spare already-saturated pixels
    int temperature = 0;     // warm (+, amber) / cool (-, blue)
    int tint = 0;            // green (-, ) / magenta (+) — manual on top of WB gains
    double wbR = 1.0;        // white-balance gains from the eyedropper
    double wbG = 1.0;
    double wbB = 1.0;

    // Detail / effects
    int denoise = 0;         // 0..100 chroma-noise smoothing, weighted to shadows
    int clarity = 0;         // midtone local contrast
    int sharpen = 0;         // 0..100 unsharp amount
    int vignette = 0;        // darken (-) / lighten (+) the corners

    // Artificial directional lighting: fakes a light source raking across a
    // surface estimated from the image's own luminance (see applyLighting in
    // Adjustments.cpp). `lightAngle` (0..360°) only matters when
    // `lightIntensity` (-100..100, 0 = off) is non-zero.
    int lightAngle = 0;
    int lightIntensity = 0;

    // Flat-color painterly/posterize stylization: blurs away fine detail,
    // then quantizes the image to a small palette (fewer colours as the
    // amount increases), producing a flat, geometric-illustration look.
    // 0 = off. Global-only (not available per-layer).
    int flatStyle = 0;

    // Tone curve: control points in [0,1]×[0,1], monotonic in x. Empty/identity
    // means no curve. Applied to all channels via a 65536-entry LUT.
    QVector<QPointF> curve;

    // Photoshop-style Levels (composite + per-channel). Applied after the curve.
    Levels levels;

    // Targeted color-range adjustments (one per completed pick+drag gesture).
    // Applied right after the Levels step.
    QVector<ColorRangeAdjust> colorRanges;

    // Adjustment layer stack, composited over the base (global) tone pass in
    // order — the "Base" layer is the global fields above; each entry here is
    // an additional layer with its own tone/colour, mask, opacity and blend.
    QVector<Mask> masks;

    // Persisted per-group state for layer groups (see Mask::groupId). A
    // group with no matching entry here (e.g. one created before this field
    // existed) falls back to defaults: fully visible, opacity 1.0, Normal
    // blend, not collapsed — see applyMasks in Adjustments.cpp for how a
    // group's own opacity/blend wraps its members like a single virtual
    // layer, and LayersPanel for the collapse/expand UI.
    QVector<MaskGroup> groups;

    // Spot-heal ops (oriented-image coords; applied before crop).
    QVector<HealOp> heals;

    // Content-aware object-removal regions (oriented-image coords, pre-crop;
    // applied same stage as heals, before crop; see RemoveObjectOp comment).
    QVector<RemoveObjectOp> removals;

    // Geometry
    int rotationQuadrants = 0; // clockwise 90° turns (0..3)
    bool flipH = false;
    bool flipV = false;
    QRect cropRect;            // oriented-image coords; null = no crop
    double cropAngle = 0.0;    // degrees, clockwise; straightens image before cropRect is applied

    // Canvas background color (view-only — never affects the rendered/exported
    // image, only what ImageCanvas paints behind the photo). Persisted per-image
    // in the sidecar like everything else here, so it's just carried along.
    QColor backgroundColor = QColor(30, 30, 30);

    // Photoshop-style ruler guides (view-only, canvas overlay; never affects
    // the rendered/exported image). Horizontal guides are a fraction of the
    // displayed image's height (0=top); vertical guides a fraction of its
    // width (0=left). Persisted per-image in the sidecar like backgroundColor.
    QVector<double> guidesH;
    QVector<double> guidesV;

    bool hasCurve() const;     // true if curve is set and not the identity

    bool operator==(const Adjustments &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance &&
               temperature == o.temperature && tint == o.tint &&
               wbR == o.wbR && wbG == o.wbG && wbB == o.wbB &&
               denoise == o.denoise && clarity == o.clarity &&
               sharpen == o.sharpen && vignette == o.vignette &&
               lightAngle == o.lightAngle && lightIntensity == o.lightIntensity &&
               flatStyle == o.flatStyle &&
               curve == o.curve && levels == o.levels &&
               colorRanges == o.colorRanges &&
               masks == o.masks && heals == o.heals &&
               removals == o.removals &&
               rotationQuadrants == o.rotationQuadrants && flipH == o.flipH &&
               flipV == o.flipV && cropRect == o.cropRect &&
               std::abs(cropAngle - o.cropAngle) < 1e-9 &&
               backgroundColor == o.backgroundColor &&
               guidesH == o.guidesH && guidesV == o.guidesV;
    }
    bool operator!=(const Adjustments &o) const { return !(*this == o); }
};

// True if any tone/colour/detail/effect adjustment is non-neutral (ignores
// geometry). Used to skip the per-pixel pass entirely when nothing is set.
bool hasToneEdits(const Adjustments &adj);

// Apply `adj` to `base`, returning a new image. Order: orientation → crop →
// white balance/tone/colour (per-pixel) → denoise/clarity/sharpen
// (convolution) → vignette. Pure and side-effect free: safe to unit-test and
// to run on a
// full-res base (export) or a display-scaled copy (interactive).
// `brushCache`, if given, is used/updated for incremental mask-stroke
// rasterization (see BrushRasterCache); pass nullptr for a one-shot exact
// render (export, tests).
// `maskSnapshotIndex`/`maskSnapshotOut`: if `maskSnapshotIndex >= 0`, writes
// the cumulative composite through and including `adj.masks[maskSnapshotIndex]`
// into `*maskSnapshotOut` (used to feed the per-layer Levels histogram — see
// LayersPanel/RetouchTab). Zero extra cost when `maskSnapshotIndex < 0`.
// `orientedToGeom`/`geomRotationDeg`/`scale`: forwarded to the Shape/TextBox
// rasterizer (see applyPaintMasks below) so those layers composite at the
// correct position/size no matter where they fall in the stack relative to
// other layers.
// `belowSnapshotIndex`/`belowSnapshotOut`: if `belowSnapshotIndex >= 0`,
// writes the composite through everything *below* (i.e. excluding)
// `adj.masks[belowSnapshotIndex]` into `*belowSnapshotOut`. Pass that image
// back in as `resumeImg` (with the same index as `resumeFromIndex`) on a
// later call to skip orientation/crop/tone and every mask below it, and only
// recomposite `resumeFromIndex` and the masks above it — used by RetouchTab
// to keep per-mouse-move brush/erase renders cheap during a drag. Not valid
// when a Background-type mask sits at or above resumeFromIndex (see
// RetouchTab::retone).
QImage applyAdjustments(const QImage &base, const Adjustments &adj,
                        QVector<BrushRasterCache> *brushCache = nullptr,
                        int maskSnapshotIndex = -1,
                        QImage *maskSnapshotOut = nullptr,
                        const QTransform &orientedToGeom = QTransform(),
                        double geomRotationDeg = 0.0, double scale = 1.0,
                        int belowSnapshotIndex = -1,
                        QImage *belowSnapshotOut = nullptr,
                        int resumeFromIndex = -1,
                        const QImage *resumeImg = nullptr,
                        // Forwarded to applyMasks' dirtyRectOut - see its doc
                        // comment. Only ever populated on the resumeImg/drag
                        // path; left untouched otherwise.
                        QRect *dirtyRectOut = nullptr);

// Composites just the "interactive tier" of masks — MaskType::Paint (the
// free-draw brush/paint tool), MaskType::Shape, and MaskType::TextBox — on
// top of `img`, in their own relative stack order. Standalone utility (not
// used by applyAdjustments's main render path, which composites every tier
// together via MaskPass::All so stack order is respected across tiers);
// useful for isolating just the interactive tier, e.g. in tests. `brushCache`,
// if given, is used/updated for incremental Paint stroke rasterization, same
// as applyAdjustments. Shape/TextBox masks are still stored in raw
// oriented-image pixel space (see Mask::shapeRect etc.), so `orientedToGeom`/
// `geomRotationDeg`/`scale` are forwarded exactly as to applyShapes/
// applyTexts to map that geometry into `img`'s local pixel space.
void applyPaintMasks(QImage &img, const QVector<Mask> &masks,
                     QVector<BrushRasterCache> *brushCache = nullptr,
                     const QTransform &orientedToGeom = QTransform(),
                     double geomRotationDeg = 0.0, double scale = 1.0);

// Paint bucket: flood-fills the contiguous region of *unpainted* pixels
// (no existing stroke coverage, no prior fill) reachable from `clickNorm`
// (width-normalized, same convention as BrushStrokePoint::pt) with `color`,
// rasterized at `w`x`h`, and returns the result merged on top of `m`'s
// existing `fillMask` (resampled to `w`x`h` first if it was captured at a
// different resolution). Returns `m.fillMask` unchanged if the click point
// itself is already painted/filled (no-op, matches clicking on existing
// content in Photoshop's bucket). No leak protection: an unclosed brush
// outline lets the fill spread to the whole unpainted region, same as
// Photoshop's default bucket behaviour.
QImage bucketFillPaintMask(const Mask &m, const QPointF &clickNorm,
                          const QColor &color, int w, int h);

// Build a tinted, semi-transparent overlay (ARGB, alpha = mask weight × maxAlpha)
// visualizing a single mask's coverage, for live "see the mask" feedback while
// editing. Computed at a reduced internal resolution (scaled by the caller).
// `source` is the (already-adjusted) preview image, used to sample colours for
// Auto Mask edge detection; pass a null QImage if the mask has autoMask off.
QImage maskCoverageOverlay(const Mask &m, int w, int h, const QColor &tint,
                           int maxAlpha = 140, const QImage &source = QImage(),
                           BrushRasterCache *cache = nullptr);

// Human-readable name of what changed between two committed snapshots. Returns
// the primary changed field's label (e.g. "Brightness", "Crop", "Spot Heal").
// "No change" if equal; "Adjust" if something differs but isn't recognized.
QString historyStepLabel(const Adjustments &prev, const Adjustments &curr);

// Quantize a 16-bit-per-channel image (applyAdjustments' output) down to 8-bit
// ARGB32 with a per-pixel ordered (Bayer) dither, instead of bare truncation.
// The editing pipeline keeps full 16-bit precision through every adjustment so
// shadow/brightness pushes don't re-expose 8-bit quantization steps, but the
// screen and PNG/JPEG/thumbnail outputs are inherently 8-bit — dithering at
// this final step turns any remaining quantization into imperceptible noise
// instead of visible banding. No-op passthrough (via convertToFormat) if
// `img` isn't 16-bit per channel.
QImage ditherTo8Bit(const QImage &img);

// Companion to ditherTo8Bit() for ImageCanvas's drag-preview dirty-rect path
// (see applyMasks' dirtyRectOut / RenderWorker::done's dirtyRect): patches
// just `r` of `out` (already Format_ARGB32, same size as `img`) from `img`,
// instead of re-dithering the whole buffer when only a small region actually
// changed since the last frame. `r` must be within `img`/`out`'s bounds.
void ditherRegionInto(QImage &out, const QImage &img, const QRect &r);

Q_DECLARE_METATYPE(Adjustments)
