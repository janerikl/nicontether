#pragma once

#include <QImage>
#include <QRect>

#include <functional>
// Exemplar-based (simplified Criminisi-style) patch inpainting, implemented
// from scratch on top of QImage (no OpenCV). Given a source image and a mask
// marking the region to remove/fill, repeatedly finds the best-matching
// source patch (lowest SSD over already-known pixels) for the highest-
// priority boundary patch of the remaining hole and copies it in, shrinking
// the hole inward until it is fully filled.
//
// `source` is the full working image (oriented, pre-crop) the caller is
// painting over. `mask` marks the region to fill: any pixel with alpha > 0
// (or, for a grayscale/no-alpha mask, any non-zero pixel) is treated as
// "to be filled". `mask` must be the same size as `source`.
// `boundingRect` is the region of interest (a padded bounding box around the
// masked area, in `source` coordinates) — only this sub-image is inpainted
// and returned, sized to `boundingRect`, for performance. Pixels in the
// returned image outside the mask (i.e. already-known pixels) are copied
// through unchanged from `source`.
namespace InpaintTool {

// `onProgress`, if set, is invoked with a 0-100 percentage as the hole is
// filled in (throttled to at most one call per percentage point). It is
// called synchronously from whichever thread calls inpaint() — if that is a
// worker thread, the caller is responsible for marshalling the callback back
// to the GUI thread (e.g. via QMetaObject::invokeMethod with
// Qt::QueuedConnection) before touching any UI.
QImage inpaint(const QImage &source, const QImage &mask, const QRect &boundingRect,
               std::function<void(int)> onProgress = nullptr);

} // namespace InpaintTool
