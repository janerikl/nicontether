#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

// A named, reusable cut-out object (e.g. an object lifted out of a photo via
// Select > Save Selection as Asset) that can be dropped into any document as
// an image-filled Shape mask. `imagePath` points at a PNG file (with alpha)
// under the app data directory -- the pixels themselves never live in
// QSettings, only this small index does. Mirrors BrushPreset/AdjustmentPreset
// but persists an image file per entry instead of scalar params.
struct AssetStamp {
    QString name;
    QString imagePath;
    QSize nativeSize; // pixel size of the stored cutout, for aspect-correct placement
    bool builtIn = false;
};

// Persists custom asset stamps: the index (name/path/size) via QSettings, the
// pixels as PNG files under QStandardPaths::AppDataLocation. No built-in
// stamps for v1 -- the library starts empty and is entirely user-populated.
class AssetStampStore {
public:
    AssetStampStore() { load(); }

    static QList<AssetStamp> builtins(); // always empty for now
    QList<AssetStamp> all() const;       // builtins + custom
    const QList<AssetStamp> &custom() const { return m_custom; }
    bool isCustom(const QString &name) const;

    // Saves `pixels` as a new PNG under the app data dir and records/updates
    // the index entry by name. Returns the stored AssetStamp (imagePath/
    // nativeSize filled in), or a default-constructed one on failure.
    AssetStamp addOrUpdate(const QString &name, const QImage &pixels);
    void remove(const QString &name); // custom only; deletes the PNG too; persists

private:
    void load();
    void save() const;
    static QString assetDir();

    QList<AssetStamp> m_custom;
};
