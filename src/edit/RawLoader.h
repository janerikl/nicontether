#pragma once

#include <QImage>
#include <QString>

// Decodes a RAW file (NEF) to a full-resolution RGB QImage using LibRaw
// (demosaic + camera white balance + sRGB gamma). This is CPU-heavy and
// blocking — call it on a worker thread (e.g. via QtConcurrent::run), never on
// the GUI thread. Returns a null QImage on failure.
namespace RawLoader {

QImage load(const QString &rawPath);

// Like load(), but also handles regular image formats (JPEG/PNG/TIFF/etc):
// tries the RAW decoder first, and falls back to QImage's built-in reader if
// that fails. Still CPU-heavy/blocking — same threading rules as load().
QImage loadAny(const QString &path);

} // namespace RawLoader
