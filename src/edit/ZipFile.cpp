#include "edit/ZipFile.h"

#include <zip.h>

#include <QFile>
#include <vector>

namespace ZipFile {

bool write(const QString &path, const QMap<QString, QByteArray> &entries) {
    // Truncate any existing file ourselves first: ZIP_TRUNCATE only takes
    // effect on zip_close(), so a failed write would otherwise leave a
    // half-written archive at `path`.
    QFile::remove(path);

    int err = 0;
    zip_t *archive = zip_open(path.toUtf8().constData(), ZIP_CREATE, &err);
    if (!archive) return false;

    // zip_source_buffer doesn't copy the data by default (freep=0), so the
    // QByteArrays must outlive zip_close() — keep them alive in `entries`
    // itself (the caller's copy, held by const-ref) for the archive's
    // lifetime here.
    bool ok = true;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        zip_source_t *src = zip_source_buffer(archive, it.value().constData(),
                                              it.value().size(), 0);
        if (!src) { ok = false; break; }
        if (zip_file_add(archive, it.key().toUtf8().constData(), src,
                         ZIP_FL_ENC_UTF_8) < 0) {
            zip_source_free(src);
            ok = false;
            break;
        }
    }

    if (!ok) {
        zip_discard(archive);
        QFile::remove(path);
        return false;
    }
    if (zip_close(archive) != 0) {
        QFile::remove(path);
        return false;
    }
    return true;
}

bool read(const QString &path, QMap<QString, QByteArray> &out) {
    int err = 0;
    zip_t *archive = zip_open(path.toUtf8().constData(), ZIP_RDONLY, &err);
    if (!archive) return false;

    const zip_int64_t n = zip_get_num_entries(archive, 0);
    bool ok = true;
    for (zip_int64_t i = 0; i < n && ok; ++i) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(archive, i, 0, &st) != 0 ||
            !(st.valid & ZIP_STAT_SIZE) || !(st.valid & ZIP_STAT_NAME)) {
            ok = false;
            break;
        }
        zip_file_t *zf = zip_fopen_index(archive, i, 0);
        if (!zf) { ok = false; break; }
        QByteArray bytes;
        bytes.resize(int(st.size));
        const zip_int64_t n_read =
            st.size > 0 ? zip_fread(zf, bytes.data(), st.size) : 0;
        zip_fclose(zf);
        if (n_read != zip_int64_t(st.size)) { ok = false; break; }
        out.insert(QString::fromUtf8(st.name), bytes);
    }

    zip_close(archive);
    return ok;
}

} // namespace ZipFile
