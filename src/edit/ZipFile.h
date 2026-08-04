#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>

// Minimal libzip wrapper: an in-memory name -> bytes archive, used by
// EditSidecar's .ploom project format so the embedded base image can be
// stored as plain binary (not base64-inflated JSON) alongside the
// adjustments JSON, both compressed.
namespace ZipFile {

// Writes `entries` into a new zip archive at `path`, overwriting any
// existing file. Returns false on failure (leaves any partial file removed).
bool write(const QString &path, const QMap<QString, QByteArray> &entries);

// Reads every entry from the zip archive at `path` into `out`. Returns false
// if the file can't be opened as a zip archive.
bool read(const QString &path, QMap<QString, QByteArray> &out);

} // namespace ZipFile
