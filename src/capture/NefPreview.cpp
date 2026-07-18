#include "capture/NefPreview.h"

#include <QFile>
#include <QByteArray>
#include <vector>
#include <algorithm>

namespace NefPreview {
namespace {

// Given the offset of a JPEG SOI (0xFFD8) in `p`, walk the JPEG segment markers
// to find the true end (one past the EOI). This correctly handles 0xFF00 byte
// stuffing, restart markers, and nested EXIF thumbnails (which live inside APP
// segments and are skipped by length), so it never mistakes RAW image bytes for
// an end-of-image marker. Returns -1 if the stream is malformed.
qsizetype jpegEnd(const uchar *p, qsizetype n, qsizetype start) {
    qsizetype i = start;
    if (!(i + 1 < n && p[i] == 0xFF && p[i + 1] == 0xD8)) return -1;
    i += 2;
    while (i + 1 < n) {
        if (p[i] != 0xFF) { ++i; continue; }
        while (i < n && p[i] == 0xFF) ++i; // skip fill bytes
        if (i >= n) break;
        uchar marker = p[i++];
        if (marker == 0xD9) return i;                 // EOI
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;                                 // standalone markers
        if (i + 1 >= n) break;
        int len = (p[i] << 8) | p[i + 1];
        if (len < 2) return -1;
        qsizetype segEnd = i + len;
        if (segEnd > n) return -1;
        if (marker == 0xDA) {
            // Start of Scan: entropy-coded data follows. Advance past it to the
            // next real marker (skipping stuffed 0xFF00 and restart markers).
            i = segEnd;
            while (i + 1 < n) {
                if (p[i] == 0xFF) {
                    uchar m2 = p[i + 1];
                    if (m2 == 0x00 || (m2 >= 0xD0 && m2 <= 0xD7)) { i += 2; continue; }
                    if (m2 == 0xFF) { ++i; continue; }
                    break; // next real marker
                }
                ++i;
            }
        } else {
            i = segEnd;
        }
    }
    return -1;
}

} // namespace

QImage extract(const QString &nefPath) {
    QFile f(nefPath);
    if (!f.open(QIODevice::ReadOnly)) return QImage();
    const QByteArray data = f.readAll();
    f.close();

    const qsizetype n = data.size();
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    // Collect every top-level JPEG stream in the file.
    std::vector<std::pair<qsizetype, qsizetype>> streams; // (start, length)
    qsizetype i = 0;
    while (i + 1 < n) {
        if (p[i] == 0xFF && p[i + 1] == 0xD8) {
            qsizetype end = jpegEnd(p, n, i);
            if (end > i) {
                streams.emplace_back(i, end - i);
                i = end;
                continue;
            }
        }
        ++i;
    }

    // Nikon stores a full-size preview plus a small thumbnail; the preview is the
    // largest stream. Try largest-first and return the first that actually
    // decodes, so a malformed candidate can't hide a good one.
    std::sort(streams.begin(), streams.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    for (const auto &s : streams) {
        QImage img;
        if (img.loadFromData(p + s.first, static_cast<int>(s.second), "JPEG") &&
            !img.isNull())
            return img;
    }
    return QImage();
}

} // namespace NefPreview
