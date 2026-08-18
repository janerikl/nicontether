#include "capture/NefPreview.h"

#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <vector>
#include <algorithm>

namespace NefPreview {
namespace {

// A photo's embedded preview is looked up from more than one place in the
// same short window (e.g. BrowseTab's grid/preview-pane on selection, then
// RetouchWindow's filmstrip on open) — this avoids re-scanning the whole
// multi-ten-MB file's bytes for each lookup. Small and UI-thread-only, so a
// plain bounded map is enough; keyed by path+size+mtime so a replaced file
// on disk isn't served a stale preview.
struct CacheKey {
    QString path;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    bool operator==(const CacheKey &o) const {
        return size == o.size && mtimeMs == o.mtimeMs && path == o.path;
    }
};

constexpr int kCacheCapacity = 8;
QList<CacheKey> g_cacheOrder;
QHash<QString, QPair<CacheKey, QImage>> g_cache; // keyed by path for lookup

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
    const QFileInfo fi(nefPath);
    const CacheKey key{nefPath, fi.size(), fi.lastModified().toMSecsSinceEpoch()};
    auto it = g_cache.constFind(nefPath);
    if (it != g_cache.constEnd() && it->first == key)
        return it->second;

    QFile f(nefPath);
    if (!f.open(QIODevice::ReadOnly)) return QImage();
    const QByteArray data = f.readAll();
    f.close();

    const qsizetype n = data.size();
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    // Collect a candidate JPEG stream at every SOI position. We must NOT skip
    // past a stream's computed end: false-positive 0xFFD8 markers inside the
    // compressed RAW data can walk to a far EOI whose range spans the real
    // embedded preview, so jumping past them would swallow the preview's SOI and
    // leave nothing decodable (the gray-placeholder bug). Every SOI is a
    // candidate; the decode-largest-first pass below discards the bogus ones.
    std::vector<std::pair<qsizetype, qsizetype>> streams; // (start, length)
    for (qsizetype i = 0; i + 1 < n; ++i) {
        if (p[i] == 0xFF && p[i + 1] == 0xD8) {
            qsizetype end = jpegEnd(p, n, i);
            if (end > i)
                streams.emplace_back(i, end - i);
        }
    }

    // Nikon stores a full-size preview plus a small thumbnail; the preview is the
    // largest stream. Try largest-first and return the first that actually
    // decodes, so a malformed candidate can't hide a good one.
    std::sort(streams.begin(), streams.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    QImage result;
    for (const auto &s : streams) {
        QImage img;
        if (img.loadFromData(p + s.first, static_cast<int>(s.second), "JPEG") &&
            !img.isNull()) {
            result = img;
            break;
        }
    }

    g_cache.insert(nefPath, {key, result});
    g_cacheOrder.append(key);
    if (g_cacheOrder.size() > kCacheCapacity) {
        g_cache.remove(g_cacheOrder.first().path);
        g_cacheOrder.removeFirst();
    }
    return result;
}

} // namespace NefPreview
