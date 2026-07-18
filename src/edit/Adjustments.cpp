#include "edit/Adjustments.h"

#include <QTransform>
#include <algorithm>
#include <cmath>

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
           std::abs(adj.wbB - 1.0) > 1e-4 || adj.hasCurve();
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

            // Curve then contrast then brightness.
            r = curveLut[clamp8(int(r))];
            g = curveLut[clamp8(int(g))];
            b = curveLut[clamp8(int(b))];
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
    if (curr.heals != prev.heals)             return QStringLiteral("Spot Heal");
    if (curr.rotationQuadrants != prev.rotationQuadrants)
        return QStringLiteral("Rotate");
    if (curr.flipH != prev.flipH || curr.flipV != prev.flipV)
        return QStringLiteral("Flip");
    if (curr.cropRect != prev.cropRect)       return QStringLiteral("Crop");
    return QStringLiteral("Adjust");
}
