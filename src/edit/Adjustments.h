#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>
#include <QMetaType>
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
               !vignette && !hasCurve() && levels.isIdentity();
    }
    bool operator==(const MaskAdjust &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance &&
               temperature == o.temperature && tint == o.tint &&
               wbR == o.wbR && wbG == o.wbG && wbB == o.wbB &&
               denoise == o.denoise && clarity == o.clarity &&
               sharpen == o.sharpen && vignette == o.vignette &&
               curve == o.curve && levels == o.levels;
    }
    bool operator!=(const MaskAdjust &o) const { return !(*this == o); }
};

enum class MaskType { Radial, Linear, Brush, Paint, None };

// How a layer's local adjustment composites over what's below it. Applied
// per-channel in sRGB space, then mixed with the layer below by mask weight
// x opacity (see blendChannel in Adjustments.cpp).
enum class BlendMode { Normal, Multiply, Screen, Overlay, SoftLight };

// One sampled point of a brush stroke (width-normalized). `erase` marks a dab
// painted while holding Alt, which subtracts coverage instead of adding it.
struct BrushStrokePoint {
    QPointF pt;
    bool erase = false;

    bool operator==(const BrushStrokePoint &o) const {
        return pt == o.pt && erase == o.erase;
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

    // Paint: flat fill color for a MaskType::Paint layer. Composited using
    // the same `stroke`/`brushRadius`/`hardness` coverage as MaskType::Brush,
    // but the layer's content is a solid fill of this color instead of a
    // tone-adjusted copy of the image below (see applyMasks in
    // Adjustments.cpp). Unused by all other mask types.
    QColor paintColor = Qt::black;

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

    // Erase-tool strokes (image layers only): canvas-normalized dabs that
    // punch feathered transparency into this layer's alpha at composite
    // time. Empty for non-image layers.
    QVector<ErasePoint> eraseStrokes;

    bool operator==(const Mask &o) const {
        return name == o.name && visible == o.visible &&
               std::abs(opacity - o.opacity) < 1e-9 && blend == o.blend &&
               type == o.type && inverted == o.inverted &&
               std::abs(feather - o.feather) < 1e-9 && center == o.center &&
               std::abs(radiusX - o.radiusX) < 1e-9 &&
               std::abs(radiusY - o.radiusY) < 1e-9 &&
               std::abs(angle - o.angle) < 1e-9 && p0 == o.p0 && p1 == o.p1 &&
               stroke == o.stroke && eraseStrokes == o.eraseStrokes &&
               std::abs(brushRadius - o.brushRadius) < 1e-9 &&
               std::abs(hardness - o.hardness) < 1e-9 && autoMask == o.autoMask &&
               adj == o.adj && paintColor == o.paintColor &&
               sourceImageOffset == o.sourceImageOffset &&
               sourceImageScale == o.sourceImageScale &&
               sourceImageLockRatio == o.sourceImageLockRatio &&
               sourceImagePath == o.sourceImagePath;
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
    int w = 0, h = 0;
    int pointCount = 0;
    double brushRadius = -1;
    double hardness = -1;
    bool autoMask = false;
    BrushStrokePoint lastPoint;
    bool valid = false;
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

    // Spot-heal ops (oriented-image coords; applied before crop).
    QVector<HealOp> heals;

    // Geometry
    int rotationQuadrants = 0; // clockwise 90° turns (0..3)
    bool flipH = false;
    bool flipV = false;
    QRect cropRect;            // oriented-image coords; null = no crop

    // Canvas background color (view-only — never affects the rendered/exported
    // image, only what ImageCanvas paints behind the photo). Persisted per-image
    // in the sidecar like everything else here, so it's just carried along.
    QColor backgroundColor = QColor(30, 30, 30);

    bool hasCurve() const;     // true if curve is set and not the identity

    bool operator==(const Adjustments &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance &&
               temperature == o.temperature && tint == o.tint &&
               wbR == o.wbR && wbG == o.wbG && wbB == o.wbB &&
               denoise == o.denoise && clarity == o.clarity &&
               sharpen == o.sharpen && vignette == o.vignette &&
               curve == o.curve && levels == o.levels &&
               colorRanges == o.colorRanges &&
               masks == o.masks && heals == o.heals &&
               rotationQuadrants == o.rotationQuadrants && flipH == o.flipH &&
               flipV == o.flipV && cropRect == o.cropRect &&
               backgroundColor == o.backgroundColor;
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
QImage applyAdjustments(const QImage &base, const Adjustments &adj,
                        QVector<BrushRasterCache> *brushCache = nullptr,
                        int maskSnapshotIndex = -1,
                        QImage *maskSnapshotOut = nullptr);

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

Q_DECLARE_METATYPE(Adjustments)
