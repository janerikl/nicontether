#include "edit/Adjustments.h"

#include <QPainter>
#include <QRectF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline int clamp16(int v) { return v < 0 ? 0 : (v > 65535 ? 65535 : v); }
inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

QImage orient(const QImage &img, const Adjustments &adj) {
    QImage out = img;
    if (adj.rotationQuadrants % 4 != 0) {
        QTransform t;
        t.rotate(90.0 * (adj.rotationQuadrants % 4));
        out = out.transformed(t);
    }
    if (adj.flipH || adj.flipV)
        out = out.mirrored(adj.flipH, adj.flipV);
    return out;
}

// Levels/Curve control points are stored normalized to [0,1] (or 0-255 for
// the historical Levels black/white points, rescaled below), independent of
// the pipeline's working bit depth.
constexpr int kLutSize = 65536; // one entry per 16-bit level

// Build a 65536-entry LUT from monotonic control points (x,y in [0,1]) via
// linear interpolation. Returns identity if fewer than two points.
void buildCurveLut(const QVector<QPointF> &pts, std::vector<int> &lut) {
    lut.resize(kLutSize);
    for (int i = 0; i < kLutSize; ++i) lut[i] = i;
    if (pts.size() < 2) return;
    QVector<QPointF> p = pts;
    std::sort(p.begin(), p.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    for (int i = 0; i < kLutSize; ++i) {
        double x = i / double(kLutSize - 1);
        double y;
        if (x <= p.first().x()) y = p.first().y();
        else if (x >= p.last().x()) y = p.last().y();
        else {
            int k = 1;
            while (k < p.size() && p[k].x() < x) ++k;
            const QPointF &a = p[k - 1];
            const QPointF &b = p[k];
            double t = (x - a.x()) / std::max(1e-6, b.x() - a.x());
            y = a.y() + t * (b.y() - a.y());
        }
        lut[i] = clamp16(int(std::lround(y * (kLutSize - 1))));
    }
}

// Build a 65536-entry LUT for one Levels channel: clip to [inBlack,inWhite],
// apply the midtone gamma, then map into the [outBlack,outWhite] output
// range. `c`'s black/white points are stored in legacy 0-255 units, so scale
// by 257 (== 65535/255 exactly) into the working 16-bit range.
void buildLevelsLut(const LevelsChannel &c, std::vector<int> &lut) {
    lut.resize(kLutSize);
    constexpr double k8to16 = 257.0;
    const double inB = c.inBlack * k8to16;
    const double inW = c.inWhite * k8to16;
    const double span = std::max(1.0, inW - inB); // guard divide-by-zero
    const double invGamma = 1.0 / std::clamp(c.gamma, 0.01, 9.99);
    const double outB = c.outBlack * k8to16;
    const double outSpan = (c.outWhite - c.outBlack) * k8to16;
    for (int i = 0; i < kLutSize; ++i) {
        double v = (i - inB) / span;
        v = clampd(v, 0.0, 1.0);
        v = std::pow(v, invGamma);
        lut[i] = clamp16(int(std::lround(outB + v * outSpan)));
    }
}

inline double smoothstep01(double t) {
    t = clampd(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Targeted color-range tolerance: pixels farther than this (redmean colour
// distance, 8-bit scale — see colorDist) from the picked target get no
// adjustment; closer pixels fade in with smoothstep falloff.
constexpr double kColorRangeTolerance = 60.0;

// One precomputed targeted color-range entry: target in 8-bit scale (matching
// the redmean distance units), delta in the 16-bit working range.
struct ColorRangeParam {
    double tr, tg, tb;
    int channel;
    double delta;
};

// Full tone/colour transform — shared by the global pass (applyAdjustments)
// and per-layer content (applyLayerContent): WB gains, curve, levels,
// contrast/brightness, highlights/shadows, saturation/vibrance.
struct ToneParams {
    double contrastFactor, brightness;
    double wbR, wbG, wbB;
    double hiAmt, shAmt, satScale, vibAmt;
    std::vector<int> curveLut;
    std::vector<int> lvlRgb, lvlR, lvlG, lvlB;
    std::vector<ColorRangeParam> colorRanges;
};

ToneParams makeToneParams(int contrast, int brightness, int highlights,
                          int shadows, int saturation, int vibrance,
                          int temperature, int tint, double wbR, double wbG,
                          double wbB, const QVector<QPointF> &curve,
                          const Levels &levels,
                          const QVector<ColorRangeAdjust> &colorRanges = {}) {
    ToneParams p;
    const double c = contrast;
    // Classic Photoshop contrast formula: a dimensionless ratio, scale-
    // invariant with respect to the working pixel bit depth.
    p.contrastFactor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    // `brightness` is an 8-bit-scale slider value (-100..100); rescale to the
    // 16-bit working range by the exact 65535/255 ratio.
    p.brightness = brightness * 257.0;
    const double tempF = temperature / 100.0;
    const double tintF = tint / 100.0;
    p.wbR = wbR * (1.0 + 0.4 * tempF);
    p.wbG = wbG * (1.0 + 0.4 * tintF);
    p.wbB = wbB * (1.0 - 0.4 * tempF);
    p.hiAmt = highlights / 100.0;
    p.shAmt = shadows / 100.0;
    p.satScale = 1.0 + saturation / 100.0;
    p.vibAmt = vibrance / 100.0;
    buildCurveLut(curve, p.curveLut);
    buildLevelsLut(levels.rgb, p.lvlRgb);
    buildLevelsLut(levels.r, p.lvlR);
    buildLevelsLut(levels.g, p.lvlG);
    buildLevelsLut(levels.b, p.lvlB);
    for (const ColorRangeAdjust &cr : colorRanges) {
        if (cr.amount == 0) continue;
        p.colorRanges.push_back({double(cr.r), double(cr.g), double(cr.b),
                                 cr.channel, cr.amount * 257.0});
    }
    return p;
}

QRgba64 applyTone(QRgba64 px, const ToneParams &p) {
    double r = px.red(), g = px.green(), b = px.blue();
    r *= p.wbR; g *= p.wbG; b *= p.wbB;
    r = p.curveLut[clamp16(int(r))];
    g = p.curveLut[clamp16(int(g))];
    b = p.curveLut[clamp16(int(b))];
    r = p.lvlR[p.lvlRgb[clamp16(int(r))]];
    g = p.lvlG[p.lvlRgb[clamp16(int(g))]];
    b = p.lvlB[p.lvlRgb[clamp16(int(b))]];
    for (const ColorRangeParam &cr : p.colorRanges) {
        // Redmean distance in 8-bit scale against the stored pick — same
        // formula as colorDist below, on the post-levels pixel so the match
        // tracks what the user sees.
        const double pr = r / 257.0, pg = g / 257.0, pb = b / 257.0;
        const double dr = pr - cr.tr, dg = pg - cr.tg, db = pb - cr.tb;
        const double rmean = (pr + cr.tr) / 2.0;
        const double d2 = (2.0 + rmean / 256.0) * dr * dr + 4.0 * dg * dg +
                          (2.0 + (255.0 - rmean) / 256.0) * db * db;
        if (d2 >= kColorRangeTolerance * kColorRangeTolerance) continue;
        const double wgt =
            1.0 - smoothstep01(std::sqrt(d2) / kColorRangeTolerance);
        if (wgt <= 0.0) continue;
        (cr.channel == 0 ? r : cr.channel == 1 ? g : b) += wgt * cr.delta;
    }
    r = p.contrastFactor * (r - 32768.0) + 32768.0 + p.brightness;
    g = p.contrastFactor * (g - 32768.0) + 32768.0 + p.brightness;
    b = p.contrastFactor * (b - 32768.0) + 32768.0 + p.brightness;
    double luma = 0.299 * r + 0.587 * g + 0.114 * b;
    double ln = clampd(luma / 65535.0, 0.0, 1.0);
    if (p.hiAmt != 0.0) {
        double d = p.hiAmt * 15420.0 * ln * ln;
        r += d; g += d; b += d;
    }
    if (p.shAmt != 0.0) {
        double d = p.shAmt * 15420.0 * (1.0 - ln) * (1.0 - ln);
        r += d; g += d; b += d;
    }
    luma = 0.299 * r + 0.587 * g + 0.114 * b;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double sat = mx > 0 ? (mx - mn) / mx : 0.0;
    double scale = p.satScale + p.vibAmt * (1.0 - sat);
    r = luma + (r - luma) * scale;
    g = luma + (g - luma) * scale;
    b = luma + (b - luma) * scale;
    return qRgba64(clamp16(int(std::lround(r))), clamp16(int(std::lround(g))),
                   clamp16(int(std::lround(b))), px.alpha());
}

// Blend one 0..255 channel of the layer's local result ("src") with the
// channel below it ("dst"), per standard Photoshop-style formulas in
// non-linear (sRGB) space.
inline double blendChannel(BlendMode mode, double dst, double src) {
    constexpr double kMax = 65535.0;
    switch (mode) {
    case BlendMode::Multiply:
        return dst * src / kMax;
    case BlendMode::Screen:
        return kMax - (kMax - dst) * (kMax - src) / kMax;
    case BlendMode::Overlay:
        return dst <= kMax / 2.0 ? (2.0 * dst * src) / kMax
                                 : kMax - 2.0 * (kMax - dst) * (kMax - src) / kMax;
    case BlendMode::SoftLight: {
        double a = dst / kMax, b = src / kMax;
        double d = a <= 0.25 ? ((16.0 * a - 12.0) * a + 4.0) * a
                              : std::sqrt(a);
        double r = b <= 0.5 ? a - (1.0 - 2.0 * b) * a * (1.0 - a)
                            : a + (2.0 * b - 1.0) * (d - a);
        return r * kMax;
    }
    case BlendMode::Normal:
    default:
        return src;
    }
}

// Mask weight [0,1] at a width-normalized point for radial/linear masks.
double radialWeight(const Mask &m, double nx, double ny) {
    double dx = nx - m.center.x(), dy = ny - m.center.y();
    double ca = std::cos(-m.angle), sa = std::sin(-m.angle);
    double ex = (dx * ca - dy * sa) / std::max(1e-6, m.radiusX);
    double ey = (dx * sa + dy * ca) / std::max(1e-6, m.radiusY);
    double d = std::sqrt(ex * ex + ey * ey);
    double inner = 1.0 - clampd(m.feather, 0.0, 1.0);
    double w;
    if (d <= inner) w = 1.0;
    else if (d >= 1.0) w = 0.0;
    else w = smoothstep01((1.0 - d) / std::max(1e-6, 1.0 - inner));
    return m.inverted ? 1.0 - w : w;
}

double linearWeight(const Mask &m, double nx, double ny) {
    double ax = m.p1.x() - m.p0.x(), ay = m.p1.y() - m.p0.y();
    double len2 = ax * ax + ay * ay;
    double t = len2 > 1e-9
                   ? ((nx - m.p0.x()) * ax + (ny - m.p0.y()) * ay) / len2
                   : 0.0;
    double w = 1.0 - smoothstep01(t); // full at p0, zero at p1
    return m.inverted ? 1.0 - w : w;
}

// Cheap perceptual ("redmean") weighted colour distance — no colourspace
// conversion needed, good enough to decide "same object or not" for Auto Mask.
inline double colorDist(QRgb a, QRgb b) {
    const double dr = qRed(a) - qRed(b);
    const double dg = qGreen(a) - qGreen(b);
    const double db = qBlue(a) - qBlue(b);
    const double rmean = (qRed(a) + qRed(b)) / 2.0;
    const double d2 = (2.0 + rmean / 256.0) * dr * dr + 4.0 * dg * dg +
                      (2.0 + (255.0 - rmean) / 256.0) * db * db;
    return std::sqrt(d2);
}

// Auto Mask tolerance: pixels farther than this (redmean colour distance) from
// the seed colour under a dab are excluded, so the stroke stops at edges.
constexpr double kAutoMaskTolerance = 45.0;

// Rasterize a brush mask into a coverage buffer (0..255) at image resolution.
// Dabs are applied in stroke order: normal dabs union (max) into the coverage,
// Alt-painted (erase) dabs subtract from it, so painting after erasing (or
// vice versa) behaves like Lightroom's brush eraser. `ref`, if non-null and
// matching (w,h), enables Auto Mask edge-aware limiting.
//
// `cache`, if given, lets repeated calls for the same mask/resolution during
// a drag only rasterize the newly-appended stroke points instead of redoing
// the whole stroke; see BrushRasterCache. Falls back to a full rebuild
// whenever the cache doesn't (or can no longer) apply.
void rasterizeBrush(const Mask &m, std::vector<uchar> &cov, int w, int h,
                    const QImage *ref = nullptr, BrushRasterCache *cache = nullptr) {
    const bool canReuse = cache && cache->valid && cache->w == w && cache->h == h &&
                          cache->brushRadius == m.brushRadius &&
                          cache->hardness == m.hardness &&
                          cache->autoMask == m.autoMask &&
                          cache->pointCount <= m.stroke.size() &&
                          (cache->pointCount == 0 ||
                           cache->lastPoint == m.stroke[cache->pointCount - 1]);

    int startIdx = 0;
    if (canReuse) {
        cov = cache->cov;
        startIdx = cache->pointCount;
    } else {
        cov.assign(size_t(w) * h, 0);
    }

    const double W = w;
    const double rad = std::max(1.0, m.brushRadius * W);
    const double inner = clampd(m.hardness, 0.0, 1.0) * rad;
    const double band = std::max(1e-6, rad - inner);
    const bool autoMask = m.autoMask && ref && ref->width() == w && ref->height() == h;
    for (int i = startIdx; i < m.stroke.size(); ++i) {
        const BrushStrokePoint &sp = m.stroke[i];
        const double px = sp.pt.x() * W, py = sp.pt.y() * W;
        const int x0 = std::max(0, int(px - rad));
        const int x1 = std::min(w - 1, int(px + rad));
        const int y0 = std::max(0, int(py - rad));
        const int y1 = std::min(h - 1, int(py + rad));
        QRgb seed = 0;
        if (autoMask)
            seed = ref->pixel(std::clamp(int(std::lround(px)), 0, w - 1),
                              std::clamp(int(std::lround(py)), 0, h - 1));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double dx = x - px, dy = y - py;
                double dist = std::sqrt(dx * dx + dy * dy);
                double v = dist <= inner ? 1.0
                           : dist >= rad ? 0.0
                                         : smoothstep01((rad - dist) / band);
                if (v <= 0.0) continue;
                if (autoMask) {
                    double cd = colorDist(ref->pixel(x, y), seed);
                    v *= 1.0 - smoothstep01(cd / kAutoMaskTolerance);
                    if (v <= 0.0) continue;
                }
                uchar iv = uchar(std::lround(v * 255.0));
                uchar &dst = cov[size_t(y) * w + x];
                if (sp.erase) dst = uchar(std::max(0, int(dst) - int(iv)));
                else if (iv > dst) dst = iv;
            }
        }
    }

    if (cache) {
        cache->cov = cov;
        cache->w = w;
        cache->h = h;
        cache->pointCount = m.stroke.size();
        cache->brushRadius = m.brushRadius;
        cache->hardness = m.hardness;
        cache->autoMask = m.autoMask;
        cache->lastPoint = m.stroke.isEmpty() ? BrushStrokePoint{} : m.stroke.last();
        cache->valid = true;
    }
}

QImage applyLayerContent(const QImage &src, const MaskAdjust &a); // fwd decl

// Scale `src` to cover a w×h target (aspect-fill) and crop to exactly that
// size. `offset` pans the crop window, with (-1,-1) = top-left, (0,0) =
// centered, (+1,+1) = bottom-right.
QImage coverFit(const QImage &src, int w, int h, const QPointF &offset) {
    if (src.isNull() || w <= 0 || h <= 0) return QImage();
    QImage scaled = src.scaled(w, h, Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    const int maxX = std::max(0, scaled.width() - w);
    const int maxY = std::max(0, scaled.height() - h);
    const double ox = clampd(offset.x(), -1.0, 1.0);
    const double oy = clampd(offset.y(), -1.0, 1.0);
    const int x = std::clamp(int(std::lround(maxX / 2.0 + ox * maxX / 2.0)), 0, maxX);
    const int y = std::clamp(int(std::lround(maxY / 2.0 + oy * maxY / 2.0)), 0, maxY);
    return scaled.copy(x, y, w, h);
}

QRectF imageLayerFrame(int canvasW, int canvasH, const Mask &m) {
    const double w = std::max(1, int(std::lround(canvasW * std::max(0.01, m.sourceImageScale.x()))));
    const double h = std::max(1, int(std::lround(canvasH * std::max(0.01, m.sourceImageScale.y()))));
    const double cx = canvasW * (0.5 + 0.5 * clampd(m.sourceImageOffset.x(), -1.0, 1.0));
    const double cy = canvasH * (0.5 + 0.5 * clampd(m.sourceImageOffset.y(), -1.0, 1.0));
    return QRectF(cx - w / 2.0, cy - h / 2.0, w, h);
}

// Apply all layers, blending each layer's full tone/colour/detail content
// into the composite-so-far by its per-pixel mask weight, opacity, and blend
// mode. Image layers substitute a cover-fit of their own source photo for
// "the composite so far" as the input to that tone pass.
//
// Iterated back-to-front: `masks` is stored/displayed top-of-stack-first (the
// LayersPanel list shows index 0 at the top row), but compositing must apply
// the bottom-most layer first so a higher layer paints over a lower one —
// hence the reverse loop.
void applyMasks(QImage &img, const QVector<Mask> &masks,
               QVector<BrushRasterCache> *brushCache = nullptr,
               int snapshotAfterIndex = -1, QImage *snapshotOut = nullptr) {
    const int w = img.width(), h = img.height();
    if (w == 0 || h == 0) return;
    const double W = w;
    std::vector<uchar> cov;
    if (brushCache && brushCache->size() != masks.size())
        brushCache->resize(masks.size());
    for (int mi = masks.size() - 1; mi >= 0; --mi) {
        const Mask &m = masks[mi];
        if (!m.visible || m.opacity <= 0.0) {
            if (mi == snapshotAfterIndex && snapshotOut) *snapshotOut = img;
            continue;
        }
        const bool imageLayer = m.isImageLayer();
        if (imageLayer && (m.sourceMissing || m.sourceImageCache.isNull())) {
            if (mi == snapshotAfterIndex && snapshotOut) *snapshotOut = img;
            continue;
        }
        const bool paintLayer = m.type == MaskType::Paint;
        if (!imageLayer && !paintLayer && m.adj.isZero()) {
            if (mi == snapshotAfterIndex && snapshotOut) *snapshotOut = img;
            continue;
        }
        if ((m.type == MaskType::Brush || paintLayer) && m.stroke.isEmpty()) {
            if (mi == snapshotAfterIndex && snapshotOut) *snapshotOut = img;
            continue;
        }
        QImage loc;
        if (imageLayer) {
            QRectF frame = imageLayerFrame(w, h, m);
            QImage fitted = coverFit(m.sourceImageCache, std::max(1, int(std::lround(frame.width()))),
                                     std::max(1, int(std::lround(frame.height()))),
                                     QPointF());
            loc = QImage(w, h, QImage::Format_ARGB32);
            loc.fill(Qt::transparent);
            QPainter p(&loc);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(frame.topLeft(), fitted);
            p.end();
            loc = applyLayerContent(loc, m.adj);
        } else if (paintLayer) {
            loc = QImage(w, h, QImage::Format_RGBA64);
            loc.fill(m.paintColor);
        } else {
            loc = applyLayerContent(img, m.adj);
        }
        if (m.type == MaskType::Brush || paintLayer)
            rasterizeBrush(m, cov, w, h, &img,
                           brushCache ? &(*brushCache)[mi] : nullptr);
        const double op = clampd(m.opacity, 0.0, 1.0);
        for (int y = 0; y < h; ++y) {
            QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
            const QRgba64 *locLine = reinterpret_cast<const QRgba64 *>(loc.scanLine(y));
            for (int x = 0; x < w; ++x) {
                double wgt;
                if (m.type == MaskType::None)
                    wgt = 1.0;
                else if (m.type == MaskType::Radial)
                    wgt = radialWeight(m, x / W, y / W);
                else if (m.type == MaskType::Linear)
                    wgt = linearWeight(m, x / W, y / W);
                else
                    wgt = cov[size_t(y) * w + x] / 255.0;
                if (imageLayer)
                    wgt *= qAlpha(locLine[x]) / 255.0;
                wgt *= op;
                if (wgt <= 0.0) continue;
                QRgba64 src = line[x];
                QRgba64 locPx = locLine[x];
                double br = blendChannel(m.blend, src.red(), locPx.red());
                double bg = blendChannel(m.blend, src.green(), locPx.green());
                double bb = blendChannel(m.blend, src.blue(), locPx.blue());
                double inv = 1.0 - wgt;
                line[x] = qRgba64(
                    clamp16(int(std::lround(src.red() * inv + br * wgt))),
                    clamp16(int(std::lround(src.green() * inv + bg * wgt))),
                    clamp16(int(std::lround(src.blue() * inv + bb * wgt))),
                    src.alpha());
            }
        }
        if (mi == snapshotAfterIndex && snapshotOut) *snapshotOut = img;
    }
}

// Separable moving-average blur — a fast approximation of a Gaussian, used for
// clarity (large radius) and sharpening (small radius). radius in pixels.
QImage boxBlur(const QImage &src, int radius) {
    if (radius < 1) return src;
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_RGBA64);
    QImage dst(w, h, QImage::Format_RGBA64);
    const int win = radius * 2 + 1;

    // Horizontal pass.
    for (int y = 0; y < h; ++y) {
        const QRgba64 *s = reinterpret_cast<const QRgba64 *>(src.scanLine(y));
        QRgba64 *t = reinterpret_cast<QRgba64 *>(tmp.scanLine(y));
        long sr = 0, sg = 0, sb = 0;
        for (int x = -radius; x <= radius; ++x) {
            const QRgba64 p = s[std::clamp(x, 0, w - 1)];
            sr += p.red(); sg += p.green(); sb += p.blue();
        }
        for (int x = 0; x < w; ++x) {
            t[x] = qRgba64(quint16(sr / win), quint16(sg / win), quint16(sb / win), 0xFFFF);
            const QRgba64 pout = s[std::clamp(x - radius, 0, w - 1)];
            const QRgba64 pin = s[std::clamp(x + radius + 1, 0, w - 1)];
            sr += pin.red() - pout.red();
            sg += pin.green() - pout.green();
            sb += pin.blue() - pout.blue();
        }
    }
    // Vertical pass.
    for (int x = 0; x < w; ++x) {
        long sr = 0, sg = 0, sb = 0;
        for (int y = -radius; y <= radius; ++y) {
            const QRgba64 p = reinterpret_cast<const QRgba64 *>(
                tmp.scanLine(std::clamp(y, 0, h - 1)))[x];
            sr += p.red(); sg += p.green(); sb += p.blue();
        }
        for (int y = 0; y < h; ++y) {
            reinterpret_cast<QRgba64 *>(dst.scanLine(y))[x] =
                qRgba64(quint16(sr / win), quint16(sg / win), quint16(sb / win), 0xFFFF);
            const QRgba64 pout = reinterpret_cast<const QRgba64 *>(
                tmp.scanLine(std::clamp(y - radius, 0, h - 1)))[x];
            const QRgba64 pin = reinterpret_cast<const QRgba64 *>(
                tmp.scanLine(std::clamp(y + radius + 1, 0, h - 1)))[x];
            sr += pin.red() - pout.red();
            sg += pin.green() - pout.green();
            sb += pin.blue() - pout.blue();
        }
    }
    return dst;
}

// Chroma-only noise reduction, weighted toward shadows: blurs a copy of the
// image, then for each pixel blends its chroma (colour, independent of luma)
// toward the blurred chroma, leaving luma untouched so edges/detail survive.
// The blend strength falls off as (1 - luma)^2, the same shape used for the
// `shadows` slider in applyTone, so near-black speckle gets smoothed heavily
// while midtones/highlights are barely touched.
void applyDenoise(QImage &img, int denoise) {
    if (denoise <= 0) return;
    const int w = img.width(), h = img.height();
    int radius = std::max(2, std::min(w, h) / 150);
    QImage blur = boxBlur(img, radius);
    double amt = denoise / 100.0;
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        const QRgba64 *bl = reinterpret_cast<const QRgba64 *>(blur.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgba64 p = line[x], q = bl[x];
            double r = p.red(), g = p.green(), b = p.blue();
            double br = q.red(), bg = q.green(), bb = q.blue();
            double luma = 0.299 * r + 0.587 * g + 0.114 * b;
            double blLuma = 0.299 * br + 0.587 * bg + 0.114 * bb;
            double shadowWeight = 1.0 - clampd(luma / 65535.0, 0.0, 1.0);
            shadowWeight *= shadowWeight;
            double k = amt * shadowWeight;
            double cr = (r - luma) + k * ((br - blLuma) - (r - luma));
            double cg = (g - luma) + k * ((bg - blLuma) - (g - luma));
            double cb = (b - luma) + k * ((bb - blLuma) - (b - luma));
            line[x] = qRgba64(clamp16(int(std::lround(luma + cr))),
                              clamp16(int(std::lround(luma + cg))),
                              clamp16(int(std::lround(luma + cb))), p.alpha());
        }
    }
}

// Midtone local-contrast boost (large-radius blur, mixed in by midtone weight).
void applyClarity(QImage &img, int clarity) {
    if (clarity == 0) return;
    const int w = img.width(), h = img.height();
    int radius = std::max(2, std::min(w, h) / 60);
    QImage blur = boxBlur(img, radius);
    double amt = clarity / 100.0;
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        const QRgba64 *bl = reinterpret_cast<const QRgba64 *>(blur.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgba64 p = line[x], q = bl[x];
            double lz = (0.299 * p.red() + 0.587 * p.green() + 0.114 * p.blue()) / 65535.0;
            double mid = 1.0 - std::abs(lz - 0.5) * 2.0; // midtone weight
            double k = amt * mid;
            int r = clamp16(int(p.red() + k * (int(p.red()) - int(q.red()))));
            int g = clamp16(int(p.green() + k * (int(p.green()) - int(q.green()))));
            int b = clamp16(int(p.blue() + k * (int(p.blue()) - int(q.blue()))));
            line[x] = qRgba64(r, g, b, p.alpha());
        }
    }
}

// Small-radius unsharp mask.
void applySharpen(QImage &img, int sharpen) {
    if (sharpen <= 0) return;
    const int w = img.width(), h = img.height();
    QImage blur = boxBlur(img, 1);
    double amt = sharpen / 100.0 * 1.5;
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        const QRgba64 *bl = reinterpret_cast<const QRgba64 *>(blur.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgba64 p = line[x], q = bl[x];
            int r = clamp16(int(p.red() + amt * (int(p.red()) - int(q.red()))));
            int g = clamp16(int(p.green() + amt * (int(p.green()) - int(q.green()))));
            int b = clamp16(int(p.blue() + amt * (int(p.blue()) - int(q.blue()))));
            line[x] = qRgba64(r, g, b, p.alpha());
        }
    }
}

void applyVignette(QImage &img, int vignette) {
    if (vignette == 0) return;
    const int w = img.width(), h = img.height();
    double amt = vignette / 100.0;
    double cx = w / 2.0, cy = h / 2.0;
    double maxd = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double dx = x - cx, dy = y - cy;
            double d = std::sqrt(dx * dx + dy * dy) / maxd; // 0..1
            double f = 1.0 + amt * d * d; // amt<0 darkens corners
            QRgba64 p = line[x];
            line[x] = qRgba64(clamp16(int(p.red() * f)),
                              clamp16(int(p.green() * f)),
                              clamp16(int(p.blue() * f)), p.alpha());
        }
    }
}

// Full per-layer content: the same tone/colour/detail pipeline as the global
// pass, applied to `src` in isolation. Used both for the base's global fields
// (via applyAdjustments) and for each additional layer's MaskAdjust — layers
// are just as capable as the base, minus geometry (which stays global).
QImage applyLayerContent(const QImage &src, const MaskAdjust &a) {
    QImage img = src.convertToFormat(QImage::Format_RGBA64);
    const int w = img.width(), h = img.height();
    ToneParams tp = makeToneParams(a.contrast, a.brightness, a.highlights,
                                   a.shadows, a.saturation, a.vibrance,
                                   a.temperature, a.tint, a.wbR, a.wbG, a.wbB,
                                   a.curve, a.levels);
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) line[x] = applyTone(line[x], tp);
    }
    applyDenoise(img, a.denoise);
    applyClarity(img, a.clarity);
    applySharpen(img, a.sharpen);
    applyVignette(img, a.vignette);
    return img;
}

} // namespace

// True if any mask carries a non-zero local adjustment, or is an image layer
// (which has visible content even with identity adjustments).
static bool hasMaskEdits(const Adjustments &adj) {
    for (const Mask &m : adj.masks) {
        if (!m.visible || m.opacity <= 0.0) continue;
        if (m.isImageLayer()) {
            if (m.sourceMissing || m.sourceImageCache.isNull()) continue;
            return true;
        }
        if (m.type == MaskType::Paint) {
            if (m.stroke.isEmpty()) continue;
            return true;
        }
        if (m.adj.isZero()) continue;
        if (m.type == MaskType::Brush && m.stroke.isEmpty()) continue;
        return true;
    }
    return false;
}

QImage maskCoverageOverlay(const Mask &m, int w, int h, const QColor &tint,
                           int maxAlpha, const QImage &source,
                           BrushRasterCache *cache) {
    if (w <= 0 || h <= 0) return QImage();
    // Compute at a reduced resolution so live painting stays responsive; the
    // caller scales it up to the display rect.
    const int longEdge = std::max(w, h);
    const double s = longEdge > 512 ? 512.0 / longEdge : 1.0;
    const int ow = std::max(1, int(w * s)), oh = std::max(1, int(h * s));
    QImage img(ow, oh, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    const double W = ow;
    std::vector<uchar> cov;
    if (m.type == MaskType::Brush) {
        if (m.stroke.isEmpty()) return img;
        QImage ref;
        const QImage *refPtr = nullptr;
        if (m.autoMask && !source.isNull()) {
            ref = (source.width() == ow && source.height() == oh)
                      ? source
                      : source.scaled(ow, oh, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
            refPtr = &ref;
        }
        rasterizeBrush(m, cov, ow, oh, refPtr, cache);
    }
    const int tr = tint.red(), tg = tint.green(), tb = tint.blue();
    for (int y = 0; y < oh; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < ow; ++x) {
            double wgt;
            if (m.type == MaskType::None)
                wgt = 1.0;
            else if (m.type == MaskType::Radial)
                wgt = radialWeight(m, x / W, y / W);
            else if (m.type == MaskType::Linear)
                wgt = linearWeight(m, x / W, y / W);
            else
                wgt = cov[size_t(y) * ow + x] / 255.0;
            if (wgt <= 0.0) continue;
            line[x] = qRgba(tr, tg, tb, int(std::lround(wgt * maxAlpha)));
        }
    }
    return img;
}

namespace {
// Classic 4x4 ordered (Bayer) dither matrix, thresholds spread evenly across
// one 8-bit quantization step (256 sub-levels of the 65536 range).
constexpr int kBayer4x4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};
inline quint8 dither16to8(quint16 v, int x, int y) {
    // Bias by a fraction of one output step (256 levels wide in 16-bit terms)
    // before truncating, so quantization error is spread as noise, not bands.
    const int bias = (kBayer4x4[y & 3][x & 3] * 256) / 16;
    return quint8(std::min(255, (v + bias) >> 8));
}
} // namespace

QImage ditherTo8Bit(const QImage &img) {
    if (img.format() != QImage::Format_RGBA64 &&
        img.format() != QImage::Format_RGBX64 &&
        img.format() != QImage::Format_RGBA64_Premultiplied)
        return img.convertToFormat(QImage::Format_ARGB32);

    const int w = img.width(), h = img.height();
    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgba64 *src = reinterpret_cast<const QRgba64 *>(img.scanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgba64 p = src[x];
            dst[x] = qRgba(dither16to8(p.red(), x, y), dither16to8(p.green(), x, y),
                           dither16to8(p.blue(), x, y), p.alpha() >> 8);
        }
    }
    return out;
}

bool Adjustments::hasCurve() const {
    if (curve.size() < 2) return false;
    for (const QPointF &p : curve)
        if (std::abs(p.x() - p.y()) > 1e-4) return true;
    return false;
}

bool hasToneEdits(const Adjustments &adj) {
    return adj.brightness || adj.contrast || adj.highlights || adj.shadows ||
           adj.saturation || adj.vibrance || adj.temperature || adj.tint ||
           adj.denoise || adj.clarity || adj.sharpen || adj.vignette ||
           std::abs(adj.wbR - 1.0) > 1e-4 || std::abs(adj.wbG - 1.0) > 1e-4 ||
           std::abs(adj.wbB - 1.0) > 1e-4 || adj.hasCurve() ||
           !adj.levels.isIdentity() || !adj.colorRanges.isEmpty() ||
           hasMaskEdits(adj);
}

QImage applyAdjustments(const QImage &base, const Adjustments &adj,
                        QVector<BrushRasterCache> *brushCache,
                        int maskSnapshotIndex, QImage *maskSnapshotOut) {
    if (base.isNull()) return base;

    QImage img = orient(base, adj);

    if (!adj.cropRect.isNull()) {
        QRect r = adj.cropRect.intersected(img.rect());
        if (r.isValid() && !r.isEmpty())
            img = img.copy(r);
    }

    if (!hasToneEdits(adj)) {
        if (maskSnapshotOut && maskSnapshotIndex >= 0) *maskSnapshotOut = img;
        return img;
    }

    img = img.convertToFormat(QImage::Format_RGBA64);
    const int w = img.width(), h = img.height();

    // --- Per-pixel: white balance, curve, levels, contrast, brightness,
    //     highlights/shadows, saturation/vibrance ---
    ToneParams tp = makeToneParams(adj.contrast, adj.brightness, adj.highlights,
                                   adj.shadows, adj.saturation, adj.vibrance,
                                   adj.temperature, adj.tint, adj.wbR, adj.wbG,
                                   adj.wbB, adj.curve, adj.levels,
                                   adj.colorRanges);
    for (int y = 0; y < h; ++y) {
        QRgba64 *line = reinterpret_cast<QRgba64 *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) line[x] = applyTone(line[x], tp);
    }

    // --- Additional layers (blend per-layer full content by weight) ---
    if (!adj.masks.isEmpty())
        applyMasks(img, adj.masks, brushCache, maskSnapshotIndex, maskSnapshotOut);

    // --- Denoise / clarity / sharpen / vignette (base layer only; layers have their own) ---
    applyDenoise(img, adj.denoise);
    applyClarity(img, adj.clarity);
    applySharpen(img, adj.sharpen);
    applyVignette(img, adj.vignette);

    return img;
}

QString historyStepLabel(const Adjustments &prev, const Adjustments &curr) {
    if (prev == curr) return QStringLiteral("No change");
    if (curr.brightness != prev.brightness)   return QStringLiteral("Brightness");
    if (curr.contrast != prev.contrast)       return QStringLiteral("Contrast");
    if (curr.highlights != prev.highlights)   return QStringLiteral("Highlights");
    if (curr.shadows != prev.shadows)         return QStringLiteral("Shadows");
    if (curr.saturation != prev.saturation)   return QStringLiteral("Saturation");
    if (curr.vibrance != prev.vibrance)       return QStringLiteral("Vibrance");
    if (curr.temperature != prev.temperature) return QStringLiteral("Temperature");
    if (curr.tint != prev.tint)               return QStringLiteral("Tint");
    if (curr.wbR != prev.wbR || curr.wbG != prev.wbG || curr.wbB != prev.wbB)
        return QStringLiteral("White Balance");
    if (curr.denoise != prev.denoise)         return QStringLiteral("Denoise");
    if (curr.clarity != prev.clarity)         return QStringLiteral("Clarity");
    if (curr.sharpen != prev.sharpen)         return QStringLiteral("Sharpen");
    if (curr.vignette != prev.vignette)       return QStringLiteral("Vignette");
    if (curr.curve != prev.curve)             return QStringLiteral("Curve");
    if (curr.levels != prev.levels)           return QStringLiteral("Levels");
    if (curr.colorRanges != prev.colorRanges) return QStringLiteral("Color Range");
    if (curr.masks != prev.masks)             return QStringLiteral("Layer");
    if (curr.heals != prev.heals)             return QStringLiteral("Spot Heal");
    if (curr.rotationQuadrants != prev.rotationQuadrants)
        return QStringLiteral("Rotate");
    if (curr.flipH != prev.flipH || curr.flipV != prev.flipV)
        return QStringLiteral("Flip");
    if (curr.cropRect != prev.cropRect)       return QStringLiteral("Crop");
    return QStringLiteral("Adjust");
}
