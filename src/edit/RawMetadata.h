#pragma once

#include <QDateTime>
#include <QString>

// Lightweight EXIF-ish metadata pulled from a RAW file via LibRaw, without a
// full unpack/decode (open_file() alone populates these fields, so this is
// cheap enough to call per-selection in the Browse tab).
namespace RawMetadata {

struct Info {
    bool valid = false;
    QString make;
    QString model;
    QString lens;
    double isoSpeed = 0;
    double aperture = 0;
    double shutterSpeed = 0; // seconds
    double focalLength = 0;  // mm
    QDateTime timestamp;
    int width = 0;
    int height = 0;
};

Info read(const QString &rawPath);

} // namespace RawMetadata
