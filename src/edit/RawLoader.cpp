#include "edit/RawLoader.h"

#include <libraw/libraw.h>

namespace RawLoader {

QImage load(const QString &rawPath) {
    LibRaw raw;

    // 8-bit sRGB output with camera white balance — a natural-looking base.
    raw.imgdata.params.output_bps = 8;
    raw.imgdata.params.output_color = 1; // sRGB
    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.no_auto_bright = 0;

    if (raw.open_file(rawPath.toUtf8().constData()) != LIBRAW_SUCCESS)
        return QImage();
    if (raw.unpack() != LIBRAW_SUCCESS)
        return QImage();
    if (raw.dcraw_process() != LIBRAW_SUCCESS)
        return QImage();

    int errc = 0;
    libraw_processed_image_t *out = raw.dcraw_make_mem_image(&errc);
    if (!out) return QImage();

    QImage result;
    if (out->type == LIBRAW_IMAGE_BITMAP && out->colors == 3 && out->bits == 8) {
        // Copy interleaved RGB into a QImage (which owns its own buffer).
        QImage img(out->width, out->height, QImage::Format_RGB888);
        const int rowBytes = out->width * 3;
        for (int y = 0; y < out->height; ++y)
            memcpy(img.scanLine(y), out->data + y * rowBytes, rowBytes);
        // Keep the base as RGB888 (3 bytes/px) to save memory with many open
        // tabs; the editing pipeline converts to ARGB32 where it needs to.
        result = img.copy(); // detach from the loop-local buffer lifetime
    }

    LibRaw::dcraw_clear_mem(out);
    raw.recycle();
    return result;
}

QImage loadAny(const QString &path) {
    QImage img = load(path);
    if (!img.isNull()) return img;
    return QImage(path);
}

} // namespace RawLoader
