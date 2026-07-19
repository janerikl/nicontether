#include "ui/GridOverlay.h"

#include <cassert>
#include <cstdio>
#include <cmath>

static bool hasSeg(const std::vector<grid::Seg> &v,
                   double x1, double y1, double x2, double y2) {
    const double eps = 1e-6;
    for (const auto &s : v) {
        if (std::fabs(s.x1 - x1) < eps && std::fabs(s.y1 - y1) < eps &&
            std::fabs(s.x2 - x2) < eps && std::fabs(s.y2 - y2) < eps) {
            return true;
        }
    }
    return false;
}

static bool inRange(const std::vector<grid::Seg> &v) {
    for (const auto &s : v) {
        if (s.x1 < -1e-9 || s.x1 > 1 + 1e-9) return false;
        if (s.y1 < -1e-9 || s.y1 > 1 + 1e-9) return false;
        if (s.x2 < -1e-9 || s.x2 > 1 + 1e-9) return false;
        if (s.y2 < -1e-9 || s.y2 > 1 + 1e-9) return false;
    }
    return true;
}

int main() {
    using grid::segments;

    // Off -> no segments.
    assert(segments(GridMode::Off).empty());

    // Thirds -> 4 lines (2 vertical, 2 horizontal) at 1/3 and 2/3, full span.
    {
        auto v = segments(GridMode::Thirds);
        assert(v.size() == 4);
        assert(hasSeg(v, 1.0/3.0, 0.0, 1.0/3.0, 1.0));  // vertical @ 1/3
        assert(hasSeg(v, 2.0/3.0, 0.0, 2.0/3.0, 1.0));  // vertical @ 2/3
        assert(hasSeg(v, 0.0, 1.0/3.0, 1.0, 1.0/3.0));  // horizontal @ 1/3
        assert(hasSeg(v, 0.0, 2.0/3.0, 1.0, 2.0/3.0));  // horizontal @ 2/3
        assert(inRange(v));
    }

    // Golden ratio -> 4 lines at phi divisions 0.382 and 0.618.
    {
        auto v = segments(GridMode::GoldenRatio);
        assert(v.size() == 4);
        const double a = 1.0 - 0.618; // 0.382
        const double b = 0.618;
        assert(hasSeg(v, a, 0.0, a, 1.0));
        assert(hasSeg(v, b, 0.0, b, 1.0));
        assert(hasSeg(v, 0.0, a, 1.0, a));
        assert(hasSeg(v, 0.0, b, 1.0, b));
        assert(inRange(v));
    }

    // Crosshair -> center vertical + horizontal.
    {
        auto v = segments(GridMode::Crosshair);
        assert(v.size() == 2);
        assert(hasSeg(v, 0.5, 0.0, 0.5, 1.0));
        assert(hasSeg(v, 0.0, 0.5, 1.0, 0.5));
    }

    // Diagonals -> both corner-to-corner diagonals present.
    {
        auto v = segments(GridMode::Diagonals);
        assert(hasSeg(v, 0.0, 0.0, 1.0, 1.0));
        assert(hasSeg(v, 1.0, 0.0, 0.0, 1.0));
        assert(inRange(v));
    }

    // Golden spiral -> non-empty polyline approximation, all in range.
    {
        auto v = segments(GridMode::GoldenSpiral);
        assert(!v.empty());
        assert(inRange(v));
    }

    std::puts("GridOverlayTest: all assertions passed");
    return 0;
}
