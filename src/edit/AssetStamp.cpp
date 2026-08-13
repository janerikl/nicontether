#include "edit/AssetStamp.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

QString AssetStampStore::assetDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                  QStringLiteral("/assetStamps");
    QDir().mkpath(dir);
    return dir;
}

QList<AssetStamp> AssetStampStore::builtins() { return {}; }

QList<AssetStamp> AssetStampStore::all() const { return builtins() + m_custom; }

bool AssetStampStore::isCustom(const QString &name) const {
    for (const AssetStamp &s : m_custom)
        if (s.name == name) return true;
    return false;
}

AssetStamp AssetStampStore::addOrUpdate(const QString &name, const QImage &pixels) {
    if (name.trimmed().isEmpty() || pixels.isNull()) return AssetStamp();

    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    QString path = QDir(assetDir()).filePath(QStringLiteral("%1.png").arg(uuid));
    if (!pixels.save(path, "PNG")) return AssetStamp();

    AssetStamp entry;
    entry.name = name;
    entry.imagePath = path;
    entry.nativeSize = pixels.size();
    entry.builtIn = false;

    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].name == name) {
            // Replacing an existing entry: drop the old PNG it pointed at.
            QFile::remove(m_custom[i].imagePath);
            m_custom[i] = entry;
            save();
            return entry;
        }
    }
    m_custom.append(entry);
    save();
    return entry;
}

void AssetStampStore::remove(const QString &name) {
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].name == name) {
            QFile::remove(m_custom[i].imagePath);
            m_custom.removeAt(i);
            save();
            return;
        }
    }
}

void AssetStampStore::load() {
    m_custom.clear();
    QSettings s;
    int n = s.beginReadArray("assetStamps");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        AssetStamp entry;
        entry.name = s.value("name").toString();
        entry.imagePath = s.value("imagePath").toString();
        entry.nativeSize = QSize(s.value("width", 0).toInt(), s.value("height", 0).toInt());
        entry.builtIn = false;
        if (!entry.name.isEmpty() && !entry.imagePath.isEmpty()) m_custom.append(entry);
    }
    s.endArray();
}

void AssetStampStore::save() const {
    QSettings s;
    s.beginWriteArray("assetStamps");
    for (int i = 0; i < m_custom.size(); ++i) {
        s.setArrayIndex(i);
        const AssetStamp &entry = m_custom[i];
        s.setValue("name", entry.name);
        s.setValue("imagePath", entry.imagePath);
        s.setValue("width", entry.nativeSize.width());
        s.setValue("height", entry.nativeSize.height());
    }
    s.endArray();
}
