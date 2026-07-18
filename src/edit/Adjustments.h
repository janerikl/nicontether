#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QMetaType>
#include <cmath>

// One channel of a Photoshop-style Levels adjustment: input black/white points
// clip and stretch the tonal range, gamma remaps the midtones, and the output
// range compresses the result. Implemented as a 256-entry LUT (see
// buildLevelsLut) — a constrained tone curve, applied composite-then-per-channel.
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
    int clarity = 0;         // midtone local contrast
    int sharpen = 0;         // 0..100 unsharp amount
    int vignette = 0;        // darken (-) / lighten (+) the corners

    // Tone curve: control points in [0,1]×[0,1], monotonic in x. Empty/identity
    // means no curve. Applied to all channels via a 256-entry LUT.
    QVector<QPointF> curve;

    // Photoshop-style Levels (composite + per-channel). Applied after the curve.
    Levels levels;

    // Spot-heal ops (oriented-image coords; applied before crop).
    QVector<HealOp> heals;

    // Geometry
    int rotationQuadrants = 0; // clockwise 90° turns (0..3)
    bool flipH = false;
    bool flipV = false;
    QRect cropRect;            // oriented-image coords; null = no crop

    bool hasCurve() const;     // true if curve is set and not the identity

    bool operator==(const Adjustments &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance &&
               temperature == o.temperature && tint == o.tint &&
               wbR == o.wbR && wbG == o.wbG && wbB == o.wbB &&
               clarity == o.clarity && sharpen == o.sharpen &&
               vignette == o.vignette && curve == o.curve && levels == o.levels &&
               heals == o.heals &&
               rotationQuadrants == o.rotationQuadrants && flipH == o.flipH &&
               flipV == o.flipV && cropRect == o.cropRect;
    }
    bool operator!=(const Adjustments &o) const { return !(*this == o); }
};

// True if any tone/colour/detail/effect adjustment is non-neutral (ignores
// geometry). Used to skip the per-pixel pass entirely when nothing is set.
bool hasToneEdits(const Adjustments &adj);

// Apply `adj` to `base`, returning a new image. Order: orientation → crop →
// white balance/tone/colour (per-pixel) → clarity/sharpen (convolution) →
// vignette. Pure and side-effect free: safe to unit-test and to run on a
// full-res base (export) or a display-scaled copy (interactive).
QImage applyAdjustments(const QImage &base, const Adjustments &adj);

// Human-readable name of what changed between two committed snapshots. Returns
// the primary changed field's label (e.g. "Brightness", "Crop", "Spot Heal").
// "No change" if equal; "Adjust" if something differs but isn't recognized.
QString historyStepLabel(const Adjustments &prev, const Adjustments &curr);

Q_DECLARE_METATYPE(Adjustments)
