#include "edit/NewDocumentSize.h"
#include <cassert>

int main() {
    // Pixels: passes through unchanged (dpi ignored).
    {
        QSize s = computeCanvasPixelSize(800, 600, SizeUnit::Pixels, 300);
        assert(s.width() == 800 && s.height() == 600);
    }
    // Inches at 300 DPI.
    {
        QSize s = computeCanvasPixelSize(4, 6, SizeUnit::Inches, 300);
        assert(s.width() == 1200 && s.height() == 1800);
    }
    // Centimeters at 96 DPI (1 inch = 2.54 cm).
    {
        QSize s = computeCanvasPixelSize(2.54, 5.08, SizeUnit::Centimeters, 96);
        assert(s.width() == 96 && s.height() == 192);
    }
    // Degenerate/zero input clamps to at least 1x1 so a blank QImage is
    // never constructed with a zero or negative dimension.
    {
        QSize s = computeCanvasPixelSize(0, -5, SizeUnit::Pixels, 300);
        assert(s.width() == 1 && s.height() == 1);
    }
    return 0;
}
