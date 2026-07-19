#include "ui/AfMapping.h"

#include <cassert>
#include <cstdio>

int main() {
    using afmap::mapClickToAf;

    // Drawn image rect at (0,0) size 640x480; AF frame 640x480 (1:1).
    // Center click -> center of AF frame.
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 640, 480);
        assert(r.valid);
        assert(r.x == 320 && r.y == 240);
    }

    // Different AF frame scale (320x240): center still maps to center.
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 320, 240);
        assert(r.valid);
        assert(r.x == 160 && r.y == 120);
    }

    // Top-left corner of drawn rect -> (0,0).
    {
        auto r = mapClickToAf(0, 0, 0, 0, 640, 480, 320, 240);
        assert(r.valid && r.x == 0 && r.y == 0);
    }

    // Letterboxed rect offset by (100,50): click at rect origin -> (0,0).
    {
        auto r = mapClickToAf(100, 50, 100, 50, 640, 480, 320, 240);
        assert(r.valid && r.x == 0 && r.y == 0);
    }

    // Click above/left of the drawn rect -> invalid.
    {
        auto r = mapClickToAf(99, 49, 100, 50, 640, 480, 320, 240);
        assert(!r.valid);
    }

    // Non-positive AF frame -> invalid (caller falls back to frame size).
    {
        auto r = mapClickToAf(320, 240, 0, 0, 640, 480, 0, 0);
        assert(!r.valid);
    }

    std::puts("AfMappingTest: all assertions passed");
    return 0;
}
