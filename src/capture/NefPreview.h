#pragma once

#include <QImage>
#include <QString>

// Extracts the large embedded JPEG preview that Nikon stores inside every NEF
// RAW file, avoiding a full demosaic. Works by scanning for the largest
// self-contained JPEG stream (SOI..EOI) in the file, which is the full-size
// preview on Nikon bodies.
namespace NefPreview {

// Returns a decoded preview image, or a null QImage on failure.
QImage extract(const QString &nefPath);

} // namespace NefPreview
