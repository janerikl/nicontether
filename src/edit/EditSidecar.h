#pragma once

#include <QString>

#include "edit/Adjustments.h"

// Non-destructive edits are stored in a JSON sidecar next to the RAW file
// (`<image>.nte.json`), so reopening a photo or session restores them.
namespace EditSidecar {

QString pathFor(const QString &imagePath);
bool exists(const QString &imagePath);
bool save(const QString &imagePath, const Adjustments &adj);
// Fills `out` and returns true if a sidecar was read successfully.
bool load(const QString &imagePath, Adjustments &out);

} // namespace EditSidecar
