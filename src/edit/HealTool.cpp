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

    // Find the smoothest source patch among candidate directions.
    static const double dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {0.707, 0.707}, {-0.707, 0.707}, {0.707, -0.707}, {-0.707, -0.707}};
    const double dist = r * 2.4;
    int bestX = 0, bestY = 0;
    double bestVar = 1e18;
    bool found = false;
    for (auto &d : dirs) {
        int sx = op.x + int(d[0] * dist);
        int sy = op.y + int(d[1] * dist);
        if (sx - r < 0 || sy - r < 0 || sx + r >= W || sy + r >= H) continue;
        double v = circleVariance(img, sx, sy, r);
        if (v < bestVar) { bestVar = v; bestX = sx; bestY = sy; found = true; }
    }
    if (!found) return; // no room to sample a source

    // Brightness match: shift source so its mean equals the target's
    // surroundings (a ring outside the healed area, so the blemish itself is
    // excluded from the reference).
    double smr, smg, smb, tmr, tmg, tmb;
    circleMean(img, bestX, bestY, r, smr, smg, smb);
    ringMean(img, op.x, op.y, r, int(r * 1.6), tmr, tmg, tmb);
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
