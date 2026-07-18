#include "edit/Adjustments.h"

#include <QTransform>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
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

// Build a 256-entry LUT from monotonic control points (x,y in [0,1]) via linear
// interpolation. Returns identity if fewer than two points.
void buildCurveLut(const QVector<QPointF> &pts, int lut[256]) {
    for (int i = 0; i < 256; ++i) lut[i] = i;
    if (pts.size() < 2) return;
    QVector<QPointF> p = pts;
    std::sort(p.begin(), p.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    for (int i = 0; i < 256; ++i) {
        double x = i / 255.0;
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
        lut[i] = clamp8(int(std::lround(y * 255.0)));
    }
}

// Build a 256-entry LUT for one Levels channel: clip to [inBlack,inWhite],
// apply the midtone gamma, then map into the [outBlack,outWhite] output range.
void buildLevelsLut(const LevelsChannel &c, int lut[256]) {
    const double inB = c.inBlack;
    const double inW = c.inWhite;
    const double span = std::max(1.0, inW - inB); // guard divide-by-zero
    const double invGamma = 1.0 / std::clamp(c.gamma, 0.01, 9.99);
    const double outB = c.outBlack;
    const double outSpan = c.outWhite - c.outBlack;
    for (int i = 0; i < 256; ++i) {
        double v = (i - inB) / span;
        v = clampd(v, 0.0, 1.0);
        v = std::pow(v, invGamma);
        lut[i] = clamp8(int(std::lround(outB + v * outSpan)));
    }
}

// Precomputed per-mask local tone/colour transform (temp/tint as WB-style
// gains, then contrast/brightness, highlights/shadows, saturation/vibrance).
struct LocalParams {
    double contrastFactor;
    double brightness;
    double wbR, wbG, wbB;
    double hiAmt, shAmt, satScale, vibAmt;
};

LocalParams makeLocalParams(const MaskAdjust &a) {
    LocalParams p;
    const double c = a.contrast;
    p.contrastFactor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    p.brightness = a.brightness;
    const double tempF = a.temperature / 100.0;
    const double tintF = a.tint / 100.0;
    p.wbR = 1.0 + 0.4 * tempF;
    p.wbG = 1.0 + 0.4 * tintF;
    p.wbB = 1.0 - 0.4 * tempF;
    p.hiAmt = a.highlights / 100.0;
    p.shAmt = a.shadows / 100.0;
    p.satScale = 1.0 + a.saturation / 100.0;
    p.vibAmt = a.vibrance / 100.0;
    return p;
}

// Apply one mask's local tone/colour to a pixel (mirrors the global per-pixel
// loop minus WB gains / curve / levels, which are global-only).
QRgb applyLocal(QRgb px, const LocalParams &p) {
    double r = qRed(px), g = qGreen(px), b = qBlue(px);
    r *= p.wbR; g *= p.wbG; b *= p.wbB;
    r = p.contrastFactor * (r - 128) + 128 + p.brightness;
    g = p.contrastFactor * (g - 128) + 128 + p.brightness;
    b = p.contrastFactor * (b - 128) + 128 + p.brightness;
    double luma = 0.299 * r + 0.587 * g + 0.114 * b;
    double ln = clampd(luma / 255.0, 0.0, 1.0);
    if (p.hiAmt != 0.0) {
        double d = p.hiAmt * 60.0 * ln * ln;
        r += d; g += d; b += d;
    }
    if (p.shAmt != 0.0) {
        double d = p.shAmt * 60.0 * (1.0 - ln) * (1.0 - ln);
        r += d; g += d; b += d;
    }
    luma = 0.299 * r + 0.587 * g + 0.114 * b;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double sat = mx > 0 ? (mx - mn) / mx : 0.0;
    double scale = p.satScale + p.vibAmt * (1.0 - sat);
    r = luma + (r - luma) * scale;
    g = luma + (g - luma) * scale;
    b = luma + (b - luma) * scale;
    return qRgba(clamp8(int(std::lround(r))), clamp8(int(std::lround(g))),
                 clamp8(int(std::lround(b))), qAlpha(px));
}

inline double smoothstep01(double t) {
    t = clampd(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
void rasterizeBrush(const Mask &m, std::vector<uchar> &cov, int w, int h,
                    const QImage *ref = nullptr) {
    cov.assign(size_t(w) * h, 0);
    const double W = w;
    const double rad = std::max(1.0, m.brushRadius * W);
    const double inner = clampd(m.hardness, 0.0, 1.0) * rad;
    const double band = std::max(1e-6, rad - inner);
    const bool autoMask = m.autoMask && ref && ref->width() == w && ref->height() == h;
    for (const BrushStrokePoint &sp : m.stroke) {
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
}

// Apply all local masks in order, blending each mask's local tone/colour into
// the (already globally-adjusted) image by its per-pixel weight.
void applyMasks(QImage &img, const QVector<Mask> &masks) {
    const int w = img.width(), h = img.height();
    if (w == 0 || h == 0) return;
    const double W = w;
    std::vector<uchar> cov;
    for (const Mask &m : masks) {
        if (m.adj.isZero()) continue;
        if (m.type == MaskType::Brush && m.stroke.isEmpty()) continue;
        const LocalParams lp = makeLocalParams(m.adj);
        if (m.type == MaskType::Brush) rasterizeBrush(m, cov, w, h, &img);
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                double wgt;
                if (m.type == MaskType::Radial)
                    wgt = radialWeight(m, x / W, y / W);
                else if (m.type == MaskType::Linear)
                    wgt = linearWeight(m, x / W, y / W);
                else
                    wgt = cov[size_t(y) * w + x] / 255.0;
                if (wgt <= 0.0) continue;
                QRgb src = line[x];
                QRgb loc = applyLocal(src, lp);
                double inv = 1.0 - wgt;
                line[x] = qRgba(
                    int(std::lround(qRed(src) * inv + qRed(loc) * wgt)),
                    int(std::lround(qGreen(src) * inv + qGreen(loc) * wgt)),
                    int(std::lround(qBlue(src) * inv + qBlue(loc) * wgt)),
                    qAlpha(src));
            }
        }
    }
}

// Separable moving-average blur — a fast approximation of a Gaussian, used for
// clarity (large radius) and sharpening (small radius). radius in pixels.
QImage boxBlur(const QImage &src, int radius) {
    if (radius < 1) return src;
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_ARGB32);
    QImage dst(w, h, QImage::Format_ARGB32);
    const int win = radius * 2 + 1;

    // Horizontal pass.
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.scanLine(y));
        QRgb *t = reinterpret_cast<QRgb *>(tmp.scanLine(y));
        long sr = 0, sg = 0, sb = 0;
        for (int x = -radius; x <= radius; ++x) {
            const QRgb p = s[std::clamp(x, 0, w - 1)];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            t[x] = qRgb(int(sr / win), int(sg / win), int(sb / win));
            const QRgb pout = s[std::clamp(x - radius, 0, w - 1)];
            const QRgb pin = s[std::clamp(x + radius + 1, 0, w - 1)];
            sr += qRed(pin) - qRed(pout);
            sg += qGreen(pin) - qGreen(pout);
            sb += qBlue(pin) - qBlue(pout);
        }
    }
    // Vertical pass.
    for (int x = 0; x < w; ++x) {
        long sr = 0, sg = 0, sb = 0;
        for (int y = -radius; y <= radius; ++y) {
            const QRgb p = reinterpret_cast<const QRgb *>(
                tmp.scanLine(std::clamp(y, 0, h - 1)))[x];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
        }
        for (int y = 0; y < h; ++y) {
            reinterpret_cast<QRgb *>(dst.scanLine(y))[x] =
                qRgb(int(sr / win), int(sg / win), int(sb / win));
            const QRgb pout = reinterpret_cast<const QRgb *>(
                tmp.scanLine(std::clamp(y - radius, 0, h - 1)))[x];
            const QRgb pin = reinterpret_cast<const QRgb *>(
                tmp.scanLine(std::clamp(y + radius + 1, 0, h - 1)))[x];
            sr += qRed(pin) - qRed(pout);
            sg += qGreen(pin) - qGreen(pout);
            sb += qBlue(pin) - qBlue(pout);
        }
    }
    return dst;
}

} // namespace

// True if any mask carries a non-zero local adjustment.
static bool hasMaskEdits(const Adjustments &adj) {
    for (const Mask &m : adj.masks) {
        if (m.adj.isZero()) continue;
        if (m.type == MaskType::Brush && m.stroke.isEmpty()) continue;
        return true;
    }
    return false;
}

QImage maskCoverageOverlay(const Mask &m, int w, int h, const QColor &tint,
                           int maxAlpha, const QImage &source) {
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
        rasterizeBrush(m, cov, ow, oh, refPtr);
    }
    const int tr = tint.red(), tg = tint.green(), tb = tint.blue();
    for (int y = 0; y < oh; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < ow; ++x) {
            double wgt;
            if (m.type == MaskType::Radial)
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

bool Adjustments::hasCurve() const {
    if (curve.size() < 2) return false;
    for (const QPointF &p : curve)
        if (std::abs(p.x() - p.y()) > 1e-4) return true;
    return false;
}

bool hasToneEdits(const Adjustments &adj) {
    return adj.brightness || adj.contrast || adj.highlights || adj.shadows ||
           adj.saturation || adj.vibrance || adj.temperature || adj.tint ||
           adj.clarity || adj.sharpen || adj.vignette ||
           std::abs(adj.wbR - 1.0) > 1e-4 || std::abs(adj.wbG - 1.0) > 1e-4 ||
           std::abs(adj.wbB - 1.0) > 1e-4 || adj.hasCurve() ||
           !adj.levels.isIdentity() || hasMaskEdits(adj);
}

QImage applyAdjustments(const QImage &base, const Adjustments &adj) {
    if (base.isNull()) return base;

    QImage img = orient(base, adj);

    if (!adj.cropRect.isNull()) {
        QRect r = adj.cropRect.intersected(img.rect());
        if (r.isValid() && !r.isEmpty())
            img = img.copy(r);
    }

    if (!hasToneEdits(adj)) return img;

    img = img.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();

    // --- Per-pixel: white balance, curve, contrast, brightness, highlights/
    //     shadows, saturation/vibrance, temperature/tint ---
    const double c = adj.contrast;
    const double contrastFactor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    int curveLut[256];
    buildCurveLut(adj.curve, curveLut);

    // Levels LUTs: composite (all channels) then per-channel.
    int lvlRgb[256], lvlR[256], lvlG[256], lvlB[256];
    buildLevelsLut(adj.levels.rgb, lvlRgb);
    buildLevelsLut(adj.levels.r, lvlR);
    buildLevelsLut(adj.levels.g, lvlG);
    buildLevelsLut(adj.levels.b, lvlB);

    const double tempF = adj.temperature / 100.0; // -1..1
    const double tintF = adj.tint / 100.0;
    const double wbR = adj.wbR * (1.0 + 0.4 * tempF);
    const double wbG = adj.wbG * (1.0 + 0.4 * tintF);
    const double wbB = adj.wbB * (1.0 - 0.4 * tempF);

    const double hiAmt = adj.highlights / 100.0;
    const double shAmt = adj.shadows / 100.0;
    const double satScale = 1.0 + adj.saturation / 100.0;
    const double vibAmt = adj.vibrance / 100.0;

    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb px = line[x];
            double r = qRed(px), g = qGreen(px), b = qBlue(px);

            // White balance (eyedropper gains + temp/tint).
            r *= wbR; g *= wbG; b *= wbB;

            // Curve, then Levels (composite then per-channel), then contrast.
            r = curveLut[clamp8(int(r))];
            g = curveLut[clamp8(int(g))];
            b = curveLut[clamp8(int(b))];
            r = lvlR[lvlRgb[clamp8(int(r))]];
            g = lvlG[lvlRgb[clamp8(int(g))]];
            b = lvlB[lvlRgb[clamp8(int(b))]];
            r = contrastFactor * (r - 128) + 128 + adj.brightness;
            g = contrastFactor * (g - 128) + 128 + adj.brightness;
            b = contrastFactor * (b - 128) + 128 + adj.brightness;

            // Highlights / shadows, weighted by luma.
            double luma = 0.299 * r + 0.587 * g + 0.114 * b;
            double ln = clampd(luma / 255.0, 0.0, 1.0);
            if (hiAmt != 0.0) {
                double wgt = ln * ln;                 // stronger in highlights
                double d = hiAmt * 60.0 * wgt;
                r += d; g += d; b += d;
            }
            if (shAmt != 0.0) {
                double wgt = (1.0 - ln) * (1.0 - ln); // stronger in shadows
                double d = shAmt * 60.0 * wgt;
                r += d; g += d; b += d;
            }

            // Saturation + vibrance.
            luma = 0.299 * r + 0.587 * g + 0.114 * b;
            double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            double sat = mx > 0 ? (mx - mn) / mx : 0.0;
            double scale = satScale + vibAmt * (1.0 - sat);
            r = luma + (r - luma) * scale;
            g = luma + (g - luma) * scale;
            b = luma + (b - luma) * scale;

            line[x] = qRgba(clamp8(int(std::lround(r))),
                            clamp8(int(std::lround(g))),
                            clamp8(int(std::lround(b))), qAlpha(px));
        }
    }

    // --- Local adjustment masks (blend per-mask tone/colour by weight) ---
    if (!adj.masks.isEmpty()) applyMasks(img, adj.masks);

    // --- Clarity (midtone local contrast) ---
    if (adj.clarity != 0) {
        int radius = std::max(2, std::min(w, h) / 60);
        QImage blur = boxBlur(img, radius);
        double amt = adj.clarity / 100.0;
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            const QRgb *bl = reinterpret_cast<const QRgb *>(blur.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x], q = bl[x];
                double lz = (0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p)) / 255.0;
                double mid = 1.0 - std::abs(lz - 0.5) * 2.0; // midtone weight
                double k = amt * mid;
                int r = clamp8(int(qRed(p) + k * (qRed(p) - qRed(q))));
                int g = clamp8(int(qGreen(p) + k * (qGreen(p) - qGreen(q))));
                int b = clamp8(int(qBlue(p) + k * (qBlue(p) - qBlue(q))));
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
    }

    // --- Sharpen (small-radius unsharp mask) ---
    if (adj.sharpen > 0) {
        QImage blur = boxBlur(img, 1);
        double amt = adj.sharpen / 100.0 * 1.5;
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            const QRgb *bl = reinterpret_cast<const QRgb *>(blur.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x], q = bl[x];
                int r = clamp8(int(qRed(p) + amt * (qRed(p) - qRed(q))));
                int g = clamp8(int(qGreen(p) + amt * (qGreen(p) - qGreen(q))));
                int b = clamp8(int(qBlue(p) + amt * (qBlue(p) - qBlue(q))));
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
    }

    // --- Vignette ---
    if (adj.vignette != 0) {
        double amt = adj.vignette / 100.0;
        double cx = w / 2.0, cy = h / 2.0;
        double maxd = std::sqrt(cx * cx + cy * cy);
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                double dx = x - cx, dy = y - cy;
                double d = std::sqrt(dx * dx + dy * dy) / maxd; // 0..1
                double f = 1.0 + amt * d * d; // amt<0 darkens corners
                QRgb p = line[x];
                line[x] = qRgba(clamp8(int(qRed(p) * f)),
                                clamp8(int(qGreen(p) * f)),
                                clamp8(int(qBlue(p) * f)), qAlpha(p));
            }
        }
    }

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
    if (curr.clarity != prev.clarity)         return QStringLiteral("Clarity");
    if (curr.sharpen != prev.sharpen)         return QStringLiteral("Sharpen");
    if (curr.vignette != prev.vignette)       return QStringLiteral("Vignette");
    if (curr.curve != prev.curve)             return QStringLiteral("Curve");
    if (curr.levels != prev.levels)           return QStringLiteral("Levels");
    if (curr.masks != prev.masks)             return QStringLiteral("Mask");
    if (curr.heals != prev.heals)             return QStringLiteral("Spot Heal");
    if (curr.rotationQuadrants != prev.rotationQuadrants)
        return QStringLiteral("Rotate");
    if (curr.flipH != prev.flipH || curr.flipV != prev.flipV)
        return QStringLiteral("Flip");
    if (curr.cropRect != prev.cropRect)       return QStringLiteral("Crop");
    return QStringLiteral("Adjust");
}
