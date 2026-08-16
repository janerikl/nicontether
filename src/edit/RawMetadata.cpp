#include "edit/RawMetadata.h"

#include <libraw/libraw.h>

namespace RawMetadata {

Info read(const QString &rawPath) {
    Info info;
    LibRaw raw;

    if (raw.open_file(rawPath.toUtf8().constData()) != LIBRAW_SUCCESS)
        return info;

    const auto &idata = raw.imgdata.idata;
    const auto &other = raw.imgdata.other;
    const auto &sizes = raw.imgdata.sizes;
    const auto &lens = raw.imgdata.lens;

    info.valid = true;
    info.make = QString::fromLatin1(idata.make).trimmed();
    info.model = QString::fromLatin1(idata.model).trimmed();
    info.lens = QString::fromLatin1(lens.Lens).trimmed();
    info.isoSpeed = other.iso_speed;
    info.aperture = other.aperture;
    info.shutterSpeed = other.shutter;
    info.focalLength = other.focal_len;
    info.timestamp = QDateTime::fromSecsSinceEpoch(other.timestamp);
    info.width = sizes.width;
    info.height = sizes.height;

    return info;
}

} // namespace RawMetadata
