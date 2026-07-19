#pragma once

#include <QString>
#include <QImage>

#include "edit/Adjustments.h"

// Non-destructive edits are stored in a JSON sidecar next to the RAW file
// (`<image>.nte.json`), so reopening a photo or session restores them.
namespace EditSidecar {

QString pathFor(const QString &imagePath);
bool exists(const QString &imagePath);
bool save(const QString &imagePath, const Adjustments &adj);
// Fills `out` and returns true if a sidecar was read successfully.
bool load(const QString &imagePath, Adjustments &out);

// A small cached JPEG rendering of the edited photo (`<image>.nte.thumb.jpg`),
// so the filmstrip can show the edited look without re-decoding the RAW and
// re-applying adjustments on every session load.
QString thumbnailPathFor(const QString &imagePath);
bool saveThumbnail(const QString &imagePath, const QImage &image);
// Returns the cached edited thumbnail, or a null QImage if none exists.
QImage loadThumbnail(const QString &imagePath);

} // namespace EditSidecar
