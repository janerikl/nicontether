#pragma once

#include <QSize>

enum class SizeUnit { Pixels, Inches, Centimeters };

// Converts a File > New dialog's width/height/unit/dpi inputs into a pixel
// QSize, clamped to a minimum of 1x1 (a blank canvas can never be 0px).
QSize computeCanvasPixelSize(double width, double height, SizeUnit unit, double dpi);
