#include "edit/NewDocumentSize.h"

#include <algorithm>
#include <cmath>

QSize computeCanvasPixelSize(double width, double height, SizeUnit unit, double dpi) {
    double wPx = width;
    double hPx = height;
    if (unit == SizeUnit::Inches) {
        wPx = width * dpi;
        hPx = height * dpi;
    } else if (unit == SizeUnit::Centimeters) {
        constexpr double kCmPerInch = 2.54;
        wPx = (width / kCmPerInch) * dpi;
        hPx = (height / kCmPerInch) * dpi;
    }
    int w = std::max(1, int(std::lround(wPx)));
    int h = std::max(1, int(std::lround(hPx)));
    return QSize(w, h);
}
