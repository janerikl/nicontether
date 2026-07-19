#pragma once

#include <vector>
#include <cmath>

// Composition overlay modes for the live view. Radio-style: one at a time.
enum class GridMode {
    Off = 0,
    Thirds = 1,
    GoldenRatio = 2,
    GoldenSpiral = 3,
    Crosshair = 4,
    Diagonals = 5,
};

namespace grid {

// A line segment in fractional coordinates: 0..1 across the drawn image rect.
struct Seg {
    double x1, y1, x2, y2;
};

inline std::vector<Seg> segments(GridMode mode) {
    std::vector<Seg> v;
    switch (mode) {
    case GridMode::Off:
        break;
    case GridMode::Thirds: {
        const double a = 1.0 / 3.0, b = 2.0 / 3.0;
        v.push_back({a, 0.0, a, 1.0});
        v.push_back({b, 0.0, b, 1.0});
        v.push_back({0.0, a, 1.0, a});
        v.push_back({0.0, b, 1.0, b});
        break;
    }
    case GridMode::GoldenRatio: {
        const double b = 0.618, a = 1.0 - b; // 0.382
        v.push_back({a, 0.0, a, 1.0});
        v.push_back({b, 0.0, b, 1.0});
        v.push_back({0.0, a, 1.0, a});
        v.push_back({0.0, b, 1.0, b});
        break;
    }
    case GridMode::Crosshair:
        v.push_back({0.5, 0.0, 0.5, 1.0});
        v.push_back({0.0, 0.5, 1.0, 0.5});
        break;
    case GridMode::Diagonals:
        v.push_back({0.0, 0.0, 1.0, 1.0});
        v.push_back({1.0, 0.0, 0.0, 1.0});
        // Harmonious "reciprocal" diagonals from remaining corners to the
        // perpendicular feet, approximated as corner-to-midpoint guides.
        v.push_back({0.0, 0.0, 1.0, 0.5});
        v.push_back({1.0, 1.0, 0.0, 0.5});
        break;
    case GridMode::GoldenSpiral: {
        // Golden spiral: nested squares tile the unit rect, each holding a
        // quarter-arc. The square's side shrinks by the golden ratio each step.
        // The arc is centered at the square corner shared with the next
        // (smaller) square, so consecutive arcs join smoothly.
        //
        // Remaining rectangle is [x0,x1] x [y0,y1]. On each step we cut the
        // square of side = min(w,h) from one end and rotate the cut side.
        const double invPhi = 0.618; // 1/golden ≈ 0.6180339887
        double x0 = 0.0, y0 = 0.0, x1 = 1.0, y1 = 1.0;
        // side cut order rotates: 0=left, 1=top, 2=right, 3=bottom.
        int sideOrder = 0;

        auto addArc = [&](double cx, double cy, double r, double a0) {
            const int N = 10;
            double prevx = cx + r * std::cos(a0);
            double prevy = cy + r * std::sin(a0);
            for (int k = 1; k <= N; ++k) {
                double a = a0 + (M_PI / 2.0) * (double(k) / N);
                double px = cx + r * std::cos(a);
                double py = cy + r * std::sin(a);
                auto clamp01 = [](double t){ return t < 0 ? 0.0 : (t > 1 ? 1.0 : t); };
                v.push_back({clamp01(prevx), clamp01(prevy),
                             clamp01(px), clamp01(py)});
                prevx = px; prevy = py;
            }
        };

        for (int i = 0; i < 9; ++i) {
            double w = x1 - x0, h = y1 - y0;
            double s = (w < h ? w : h);
            if (s < 1e-3) break;
            switch (sideOrder) {
            case 0: // square on the LEFT; arc centre at its inner (right) corner
                // square = [x0, x0+s] x [y0, y1]; inner corner = (x0+s, y1),
                // quarter sweeps from angle pi (left) to 3pi/2 (up).
                addArc(x0 + s, y1, s, M_PI);
                x0 += s;
                break;
            case 1: // square on the TOP; inner corner = (x0, y0+s)
                addArc(x0, y0 + s, s, -M_PI / 2.0);
                y0 += s;
                break;
            case 2: // square on the RIGHT; inner corner = (x1-s, y0)
                addArc(x1 - s, y0, s, 0.0);
                x1 -= s;
                break;
            case 3: // square on the BOTTOM; inner corner = (x1, y1-s)
                addArc(x1, y1 - s, s, M_PI / 2.0);
                y1 -= s;
                break;
            }
            sideOrder = (sideOrder + 1) % 4;
            (void)invPhi; // ratio emerges from repeatedly cutting the square
        }
        break;
    }
    }
    return v;
}

} // namespace grid
