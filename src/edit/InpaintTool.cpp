#include "edit/InpaintTool.h"

#include <QVector>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kPatchRadius = 5;      // 11x11 patches — smoother matches on gradient surfaces (e.g. marble)
constexpr int kPatchSize = kPatchRadius * 2 + 1;
constexpr int kMaxRandomSamples = 60; // extra far-away candidates per iteration, for quality/perf balance

// Patch search (the dominant per-iteration cost) is split across this many
// lanes via QtConcurrent, reusing Qt's global thread pool rather than
// spawning OS threads per iteration.
int searchThreadCount() { return std::max(1, QThread::idealThreadCount()); }

inline bool isHolePixel(const QImage &mask, int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= mask.width() || sy >= mask.height()) return false;
    QRgb px = mask.pixel(sx, sy);
    if (mask.hasAlphaChannel() && qAlpha(px) > 0) return true;
    // No-alpha mask: treat any non-black pixel as "fill this".
    return !mask.hasAlphaChannel() && (qRed(px) | qGreen(px) | qBlue(px)) != 0;
}

} // namespace

namespace InpaintTool {

QImage inpaint(const QImage &source, const QImage &mask, const QRect &boundingRect,
               std::function<void(int)> onProgress) {
    if (source.isNull() || mask.isNull() || boundingRect.isEmpty())
        return QImage();

    const QRect srcRect = source.rect();
    const QRect want = boundingRect.intersected(srcRect);
    if (want.isEmpty()) return QImage();

    // Working window: hole bounding box padded by a couple of patch widths on
    // each side (for exemplar search context), clamped to the source image,
    // and further capped so pathological huge strokes stay bounded. The pad
    // cap is kept fairly tight — exemplar search rarely benefits from very
    // distant patches, and this directly bounds window area (and therefore
    // search cost, which is quadratic-ish in window size).
    int pad = std::max({want.width(), want.height()}); // search radius ~ hole size
    pad = std::clamp(pad, kPatchSize * 3, 160);
    QRect win = want.adjusted(-pad, -pad, pad, pad).intersected(srcRect);
    if (win.isEmpty()) return QImage();

    const int W = win.width(), H = win.height();
    const int ox = win.x(), oy = win.y();

    // Working buffers, local to the window.
    QImage work = source.copy(win).convertToFormat(QImage::Format_ARGB32);
    QVector<bool> hole(W * H, false);
    QVector<double> confidence(W * H, 1.0);
    int holeCount = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            bool h = isHolePixel(mask, ox + x, oy + y);
            hole[y * W + x] = h;
            confidence[y * W + x] = h ? 0.0 : 1.0;
            if (h) ++holeCount;
        }
    }
    if (holeCount == 0) {
        // Nothing to fill — just return the known content.
        return work.copy(want.translated(-ox, -oy));
    }
    const int initialHoleCount = holeCount;
    const QVector<bool> wasHole = hole; // for the seam-softening pass below

    auto idx = [W](int x, int y) { return y * W + x; };
    auto inWin = [W, H](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; };

    // Reference color statistics of the material actually touching the hole
    // (its immediate ring of known neighbours) — used below to reject source
    // patches that don't plausibly belong to the same surface. Without this,
    // an isolated object sitting on a mostly-uniform background (e.g. a plate
    // of food on marble) can still pull in unrelated content from elsewhere
    // in the search window (e.g. a nearby salad bowl) if a few pixels happen
    // to line up on raw SSD alone — the ring is overwhelmingly the correct
    // background material, so it's a reliable discriminator even though the
    // window itself may contain other, unrelated content.
    double ringR = 0, ringG = 0, ringB = 0;
    int ringN = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!hole[idx(x, y)]) continue;
            bool touchesKnown = false;
            static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &d : nb) {
                int nx = x + d[0], ny = y + d[1];
                if (inWin(nx, ny) && !hole[idx(nx, ny)]) { touchesKnown = true; break; }
            }
            if (!touchesKnown) continue;
            for (auto &d : nb) {
                int nx = x + d[0], ny = y + d[1];
                if (!inWin(nx, ny) || hole[idx(nx, ny)]) continue;
                QRgb c = reinterpret_cast<const QRgb *>(work.constScanLine(ny))[nx];
                ringR += qRed(c); ringG += qGreen(c); ringB += qBlue(c);
                ++ringN;
            }
        }
    }
    double ringMeanR = ringN ? ringR / ringN : 0, ringMeanG = ringN ? ringG / ringN : 0,
           ringMeanB = ringN ? ringB / ringN : 0;
    double ringVar = 0;
    if (ringN > 0) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (!hole[idx(x, y)]) continue;
                static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto &d : nb) {
                    int nx = x + d[0], ny = y + d[1];
                    if (!inWin(nx, ny) || hole[idx(nx, ny)]) continue;
                    QRgb c = reinterpret_cast<const QRgb *>(work.constScanLine(ny))[nx];
                    double dr = qRed(c) - ringMeanR, dg = qGreen(c) - ringMeanG, db = qBlue(c) - ringMeanB;
                    ringVar += dr * dr + dg * dg + db * db;
                }
            }
        }
        ringVar /= ringN;
    }
    // Floor the spread so a very flat/uniform surrounding surface doesn't
    // make the gate impossibly strict.
    const double ringSpread = std::max(ringVar, 400.0); // ~20 gray levels squared

    // Incrementally-maintained set of boundary hole pixels (hole pixels
    // adjacent to at least one known pixel), so we never need to rescan the
    // whole window to find the next pixel to fill. Encoded as y*W+x so a
    // QSet gives O(1) membership/removal.
    QSet<int> boundary;
    auto isBoundary = [&](int x, int y) -> bool {
        if (!hole[idx(x, y)]) return false;
        static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &d : nb) {
            int nx = x + d[0], ny = y + d[1];
            if (inWin(nx, ny) && !hole[idx(nx, ny)]) return true;
        }
        return false;
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (isBoundary(x, y)) boundary.insert(idx(x, y));

    // Priority: average confidence of known neighbours in the patch.
    auto priorityAt = [&](int x, int y) -> double {
        double sum = 0; int n = 0;
        for (int dy = -kPatchRadius; dy <= kPatchRadius; ++dy) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                int nx = x + dx, ny = y + dy;
                if (!inWin(nx, ny) || hole[idx(nx, ny)]) continue;
                sum += confidence[idx(nx, ny)];
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };

    // Patch SSD over currently-known pixels only, between a target patch
    // (centered tx,ty, some pixels unknown/hole) and a fully-known candidate
    // patch centered sx,sy. Returns a huge value if the candidate patch
    // itself touches the hole (so we never copy garbage back in). Overlap
    // count is a property of the *target* patch (same for every candidate),
    // so it must never hard-reject a match — near a concave part of the hole
    // boundary a target patch can legitimately have very few known
    // neighbours, and a hard overlap floor would reject every candidate
    // simultaneously and stall the fill with most of the hole still empty.
    // Instead, low-overlap matches are just mildly deprioritized (less
    // trustworthy statistic) via a cost penalty that fades out as n grows.
    const double diag2 = double(W) * W + double(H) * H;
    auto patchCost = [&](int tx, int ty, int sx, int sy) -> double {
        if (sx - kPatchRadius < 0 || sy - kPatchRadius < 0 ||
            sx + kPatchRadius >= W || sy + kPatchRadius >= H)
            return std::numeric_limits<double>::max();
        double cost = 0;
        int n = 0;
        for (int dy = -kPatchRadius; dy <= kPatchRadius; ++dy) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                int txx = tx + dx, tyy = ty + dy;
                if (!inWin(txx, tyy) || hole[idx(txx, tyy)]) continue;
                int sxx = sx + dx, syy = sy + dy;
                if (hole[idx(sxx, syy)]) return std::numeric_limits<double>::max();
                QRgb a = reinterpret_cast<const QRgb *>(work.constScanLine(tyy))[txx];
                QRgb b = reinterpret_cast<const QRgb *>(work.constScanLine(syy))[sxx];
                int dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b), db = qBlue(a) - qBlue(b);
                cost += dr * dr + dg * dg + db * db;
                ++n;
            }
        }
        if (n == 0) return std::numeric_limits<double>::max();
        double meanCost = cost / n;
        double overlapPenalty = 1.0 + std::max(0.0, (24.0 - n) / 24.0); // up to 2x when n is tiny
        // Mild locality bias: prefer nearby source material over distant
        // regions of the image that happen to score similarly on the (noisy,
        // partial-overlap) SSD alone — e.g. a bowl of salad several patch
        // radii away from an isolated patch of counter-top shouldn't be
        // picked just because a handful of overlapping pixels lined up.
        double dx = tx - sx, dy = ty - sy;
        double distNorm = (dx * dx + dy * dy) / diag2; // 0..~1
        // Background-plausibility gate: candidates whose overall color is far
        // from what actually surrounds the hole (the ring statistics above)
        // are heavily penalized, regardless of how well a handful of pixels
        // happened to align — this is what actually keeps unrelated content
        // (e.g. a green salad several patches away) from ever winning against
        // genuine background material, which raw SSD alone can't guarantee.
        double sr = 0, sg = 0, sb = 0;
        for (int dy2 = -kPatchRadius; dy2 <= kPatchRadius; ++dy2)
            for (int dx2 = -kPatchRadius; dx2 <= kPatchRadius; ++dx2) {
                QRgb c = reinterpret_cast<const QRgb *>(work.constScanLine(sy + dy2))[sx + dx2];
                sr += qRed(c); sg += qGreen(c); sb += qBlue(c);
            }
        int patchN = kPatchSize * kPatchSize;
        sr /= patchN; sg /= patchN; sb /= patchN;
        double rr = sr - ringMeanR, rg = sg - ringMeanG, rb = sb - ringMeanB;
        double plausibility = (rr * rr + rg * rg + rb * rb) / ringSpread; // ~0 = matches ring, grows fast if not
        double gate = 1.0 + 8.0 * plausibility * plausibility;
        return meanCost * overlapPenalty * (1.0 + 10.0 * distNorm) * gate;
    };

    // Grid search stride: coarser for larger windows so total candidate
    // patches examined per iteration stays bounded regardless of hole size.
    const int windowArea = W * H;
    int step = kPatchRadius / 2;
    if (windowArea > 140 * 140) step = kPatchRadius * 3;
    else if (windowArea > 90 * 90) step = kPatchRadius * 2;
    step = std::max(1, step);

    QRandomGenerator &rng = *QRandomGenerator::global(); // thread-safe, used from the parallel lanes below
    const int kSearchThreads = searchThreadCount();

    int lastReportedPercent = -1;
    auto reportProgress = [&]() {
        if (!onProgress || initialHoleCount <= 0) return;
        int filled = initialHoleCount - holeCount;
        int percent = int((qint64(filled) * 100) / initialHoleCount);
        if (percent != lastReportedPercent) {
            lastReportedPercent = percent;
            onProgress(percent);
        }
    };

    // Iteratively fill boundary patches, shrinking the hole inward — a
    // simplified Criminisi exemplar-fill: no gradient/isophote data term,
    // just confidence-weighted boundary priority, which is enough for small
    // brush-marked object-removal regions.
    while (holeCount > 0) {
        // Find the highest-priority pixel among the (incrementally
        // maintained) boundary set only — no full-window rescan.
        int bestX = -1, bestY = -1;
        double bestPriority = -1.0;
        for (int enc : boundary) {
            int x = enc % W, y = enc / W;
            double priority = priorityAt(x, y);
            if (priority > bestPriority) { bestPriority = priority; bestX = x; bestY = y; }
        }
        if (bestX < 0) break; // shouldn't happen, but guard against infinite loop

        // Search for the best-matching fully-known source patch: a grid scan
        // over the window (already restricted to a reasonable search area,
        // with an adaptive stride) plus some random samples for extra
        // variety/perf headroom. This is the dominant per-iteration cost and
        // every candidate is independent, so it's split across CPU cores —
        // each lane scans a disjoint slice of grid rows/random samples and
        // writes only to its own result slot (no shared mutation, no locks
        // needed), then the lane results are reduced to a single best below.
        struct Best { double cost = std::numeric_limits<double>::max(); int sx = -1, sy = -1; };
        QVector<Best> laneBest(kSearchThreads);
        QVector<int> lanes(kSearchThreads);
        for (int t = 0; t < kSearchThreads; ++t) lanes[t] = t;
        QtConcurrent::blockingMap(lanes, [&](int t) {
            Best local;
            int rowIdx = 0;
            for (int sy = kPatchRadius; sy < H - kPatchRadius; sy += step, ++rowIdx) {
                if (rowIdx % kSearchThreads != t) continue;
                for (int sx = kPatchRadius; sx < W - kPatchRadius; sx += step) {
                    if (hole[idx(sx, sy)]) continue;
                    double c = patchCost(bestX, bestY, sx, sy);
                    if (c < local.cost) { local.cost = c; local.sx = sx; local.sy = sy; }
                }
            }
            for (int i = t; i < kMaxRandomSamples; i += kSearchThreads) {
                int sx = kPatchRadius + int(rng.bounded(std::max(1, W - 2 * kPatchRadius)));
                int sy = kPatchRadius + int(rng.bounded(std::max(1, H - 2 * kPatchRadius)));
                if (hole[idx(sx, sy)]) continue;
                double c = patchCost(bestX, bestY, sx, sy);
                if (c < local.cost) { local.cost = c; local.sx = sx; local.sy = sy; }
            }
            laneBest[t] = local;
        });
        int sbx = -1, sby = -1;
        double sbCost = std::numeric_limits<double>::max();
        for (const Best &b : laneBest)
            if (b.cost < sbCost) { sbCost = b.cost; sbx = b.sx; sby = b.sy; }
        if (sbx < 0) break; // no valid source patch found anywhere — give up gracefully

        // Copy the candidate patch's hole pixels into the target patch, and
        // incrementally update the hole/boundary/confidence bookkeeping only
        // around the pixels that just got filled (and their neighbours,
        // which may newly become boundary pixels).
        for (int dy = -kPatchRadius; dy <= kPatchRadius; ++dy) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                int tx = bestX + dx, ty = bestY + dy;
                if (!inWin(tx, ty) || !hole[idx(tx, ty)]) continue;
                int sx = sbx + dx, sy = sby + dy;
                if (!inWin(sx, sy) || hole[idx(sx, sy)]) continue;
                QRgb color = reinterpret_cast<const QRgb *>(work.constScanLine(sy))[sx];
                reinterpret_cast<QRgb *>(work.scanLine(ty))[tx] = color;
                hole[idx(tx, ty)] = false;
                confidence[idx(tx, ty)] = bestPriority;
                --holeCount;
                boundary.remove(idx(tx, ty));

                // Neighbours of a newly-filled pixel may now be boundary
                // pixels themselves (if still hole) — recheck just those.
                static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto &d : nb) {
                    int nx = tx + d[0], ny = ty + d[1];
                    if (!inWin(nx, ny) || !hole[idx(nx, ny)]) continue;
                    int nenc = idx(nx, ny);
                    if (isBoundary(nx, ny)) boundary.insert(nenc);
                    else boundary.remove(nenc);
                }
            }
        }
        reportProgress();
    }

    if (onProgress && lastReportedPercent < 100) onProgress(100);

    // Seam-softening pass: patch-based fill can look faintly "tiled" on
    // smooth/gradient surfaces (e.g. marble) since each 9x9 patch is copied
    // verbatim. Blend every originally-hole pixel 50/50 with its 3x3
    // neighbourhood average (which by now is fully filled) to soften hard
    // patch boundaries without erasing the copied texture entirely.
    QImage smoothed = work;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!wasHole[idx(x, y)]) continue;
            int sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!inWin(nx, ny)) continue;
                    QRgb c = reinterpret_cast<const QRgb *>(work.constScanLine(ny))[nx];
                    sr += qRed(c); sg += qGreen(c); sb += qBlue(c); sa += qAlpha(c);
                    ++n;
                }
            }
            QRgb orig = reinterpret_cast<const QRgb *>(work.constScanLine(y))[x];
            int br = (qRed(orig) + sr / n) / 2;
            int bg = (qGreen(orig) + sg / n) / 2;
            int bb = (qBlue(orig) + sb / n) / 2;
            int ba = (qAlpha(orig) + sa / n) / 2;
            reinterpret_cast<QRgb *>(smoothed.scanLine(y))[x] = qRgba(br, bg, bb, ba);
        }
    }

    return smoothed.copy(want.translated(-ox, -oy));
}

} // namespace InpaintTool
