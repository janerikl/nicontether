#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QMetaType>

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

    // Geometry
    int rotationQuadrants = 0; // clockwise 90° turns (0..3)
    bool flipH = false;
    bool flipV = false;
    QRect cropRect;            // oriented-image coords; null = no crop

    bool hasCurve() const;     // true if curve is set and not the identity
};

// True if any tone/colour/detail/effect adjustment is non-neutral (ignores
// geometry). Used to skip the per-pixel pass entirely when nothing is set.
bool hasToneEdits(const Adjustments &adj);

// Apply `adj` to `base`, returning a new image. Order: orientation → crop →
// white balance/tone/colour (per-pixel) → clarity/sharpen (convolution) →
// vignette. Pure and side-effect free: safe to unit-test and to run on a
// full-res base (export) or a display-scaled copy (interactive).
QImage applyAdjustments(const QImage &base, const Adjustments &adj);

Q_DECLARE_METATYPE(Adjustments)
