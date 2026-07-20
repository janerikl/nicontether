#include "edit/RawLoader.h"

#include <libraw/libraw.h>

namespace RawLoader {

QImage load(const QString &rawPath) {
    LibRaw raw;

    // 16-bit sRGB output with camera white balance — a natural-looking base.
    // 16-bit preserves shadow tonal resolution that 8-bit throws away, which
    // otherwise reappears as banding when shadows/brightness are pushed later.
    raw.imgdata.params.output_bps = 16;
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
    if (out->type == LIBRAW_IMAGE_BITMAP && out->colors == 3 && out->bits == 16) {
        // LibRaw's mem image is native-endian, interleaved RGB (no alpha),
        // scaled to the full 16-bit range. QImage has no packed-3x16 format,
        // so expand into RGBA64 (alpha forced opaque) — 8 bytes/px, twice
        // RGB888's footprint, accepted in exchange for eliminating 8-bit
        // shadow banding through the whole editing pipeline.
        QImage img(out->width, out->height, QImage::Format_RGBA64);
        const auto *src = reinterpret_cast<const quint16 *>(out->data);
        for (int y = 0; y < out->height; ++y) {
            auto *dst = reinterpret_cast<QRgba64 *>(img.scanLine(y));
            const quint16 *row = src + static_cast<size_t>(y) * out->width * 3;
            for (int x = 0; x < out->width; ++x) {
                const quint16 r = row[x * 3 + 0];
                const quint16 g = row[x * 3 + 1];
                const quint16 b = row[x * 3 + 2];
                dst[x] = qRgba64(r, g, b, 0xFFFF);
            }
        }
        result = img.copy(); // detach from the loop-local buffer lifetime
    }

    LibRaw::dcraw_clear_mem(out);
    raw.recycle();
    return result;
}

QImage loadAny(const QString &path) {
    QImage img = load(path);
    if (!img.isNull()) return img;
    // Qt's JPEG/PNG decoders only ever yield 8-bit data, but upconvert to
    // RGBA64 so every image reaching the Adjustments pipeline is uniformly
    // 16-bit — callers never need to branch on source bit depth.
    return QImage(path).convertToFormat(QImage::Format_RGBA64);
}

} // namespace RawLoader
