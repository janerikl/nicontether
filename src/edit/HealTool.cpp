#include "edit/HealTool.h"

#include <cmath>
#include <algorithm>

namespace {

inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Mean colour over a filled circle.
void circleMean(const QImage &img, int cx, int cy, int r,
                double &mr, double &mg, double &mb) {
    double sr = 0, sg = 0, sb = 0;
    long n = 0;
    for (int dy = -r; dy <= r; dy += 2) {
        for (int dx = -r; dx <= r; dx += 2) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
            QColor c = img.pixelColor(x, y);
            sr += c.red(); sg += c.green(); sb += c.blue();
            ++n;
        }
    }
    if (n == 0) { mr = mg = mb = 0; return; }
    mr = sr / n; mg = sg / n; mb = sb / n;
}

// Mean colour over an annulus (surroundings), ignoring the blemish inside.
void ringMean(const QImage &img, int cx, int cy, int rInner, int rOuter,
              double &mr, double &mg, double &mb) {
    double sr = 0, sg = 0, sb = 0;
    long n = 0;
    for (int dy = -rOuter; dy <= rOuter; dy += 2) {
        for (int dx = -rOuter; dx <= rOuter; dx += 2) {
            int d2 = dx * dx + dy * dy;
            if (d2 < rInner * rInner || d2 > rOuter * rOuter) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
            QColor c = img.pixelColor(x, y);
            sr += c.red(); sg += c.green(); sb += c.blue();
            ++n;
        }
    }
    if (n == 0) { mr = mg = mb = 0; return; }
    mr = sr / n; mg = sg / n; mb = sb / n;
}

// Luma variance over a filled circle — low means smooth (a good source patch).
double circleVariance(const QImage &img, int cx, int cy, int r) {
    double sum = 0, sum2 = 0;
    long n = 0;
    for (int dy = -r; dy <= r; dy += 3) {
        for (int dx = -r; dx <= r; dx += 3) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
            QColor c = img.pixelColor(x, y);
            double l = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
            sum += l; sum2 += l * l; ++n;
        }
    }
    if (n < 2) return 1e18;
    double mean = sum / n;
    return sum2 / n - mean * mean;
}

} // namespace

void applyHealOp(QImage &img, const HealOp &op) {
    const int r = op.radius;
    const int W = img.width(), H = img.height();
    if (r < 1 || W == 0 || H == 0) return;

    // Reference colour of the surroundings (a ring outside the healed area,
    // so the blemish itself is excluded), used both to steer source-patch
    // selection and to colour-match the copied patch.
    double tmr, tmg, tmb;
    ringMean(img, op.x, op.y, r, int(r * 1.6), tmr, tmg, tmb);

    // Find the best source patch among candidate directions/distances: one
    // that is both smooth (low variance) and close in colour to the target's
    // surroundings, so a same-toned patch is preferred over a merely-flat one
    // that happens to be a different shade.
    static const double dirs[16][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {0.924, 0.383}, {0.383, 0.924}, {-0.383, 0.924}, {-0.924, 0.383},
        {-0.924, -0.383}, {-0.383, -0.924}, {0.383, -0.924}, {0.924, -0.383},
        {0.707, 0.707}, {-0.707, 0.707}, {0.707, -0.707}, {-0.707, -0.707}};
    static const double dists[2] = {2.2, 2.8};
    int bestX = 0, bestY = 0;
    double bestScore = 1e18;
    bool found = false;
    if (W <= 2 * r || H <= 2 * r) return; // image too small to sample any patch
    for (double distMul : dists) {
        const double dist = r * distMul;
        for (auto &d : dirs) {
            int sx = op.x + int(d[0] * dist);
            int sy = op.y + int(d[1] * dist);
            // Clamp into bounds rather than discarding: keeps candidates
            // usable near canvas edges/corners instead of silently failing
            // to find any source patch at all.
            sx = std::clamp(sx, r, W - 1 - r);
            sy = std::clamp(sy, r, H - 1 - r);
            // Clamping can pull a candidate back toward the target near an
            // edge/corner; skip it if the source and target discs would
            // overlap, since the compositing pass reads and writes the same
            // image in place and requires clean (non-overlapping) source data.
            double ddx = sx - op.x, ddy = sy - op.y;
            if (ddx * ddx + ddy * ddy < double(4 * r * r)) continue;
            double v = circleVariance(img, sx, sy, r);
            double smr, smg, smb;
            circleMean(img, sx, sy, r, smr, smg, smb);
            double cdr = smr - tmr, cdg = smg - tmg, cdb = smb - tmb;
            double colorDist2 = cdr * cdr + cdg * cdg + cdb * cdb;
            // Colour mismatch dominates the score so a same-toned but
            // slightly textured patch beats a flat but wrong-toned one.
            double score = v + colorDist2 * 4.0;
            if (score < bestScore) { bestScore = score; bestX = sx; bestY = sy; found = true; }
        }
    }
    if (!found) return; // no room to sample a source

    // Brightness/colour match: shift source so its mean equals the target's
    // surroundings.
    double smr, smg, smb;
    circleMean(img, bestX, bestY, r, smr, smg, smb);
    const double dr = tmr - smr, dg = tmg - smg, db = tmb - smb;

    // Copy source→target with a feathered alpha (source patch does not overlap
    // the target since dist = 2.4r > 2r, so reads stay clean while we write).
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            double dd = std::sqrt(double(dx * dx + dy * dy));
            if (dd > r) continue;
            int tx = op.x + dx, ty = op.y + dy;
            if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
            int sx = bestX + dx, sy = bestY + dy;
            double t = 1.0 - dd / r;
            double a = t * t * (3.0 - 2.0 * t); // smoothstep feather
            QColor sc = img.pixelColor(sx, sy);
            QColor tc = img.pixelColor(tx, ty);
            int rr = clamp8(int(sc.red() + dr));
            int gg = clamp8(int(sc.green() + dg));
            int bb = clamp8(int(sc.blue() + db));
            img.setPixelColor(tx, ty,
                QColor(int(tc.red() * (1 - a) + rr * a),
                       int(tc.green() * (1 - a) + gg * a),
                       int(tc.blue() * (1 - a) + bb * a)));
        }
    }
}

void applyHeal(QImage &img, const QVector<HealOp> &ops) {
    for (const HealOp &op : ops)
        applyHealOp(img, op);
}
