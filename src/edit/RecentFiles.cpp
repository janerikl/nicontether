#include "edit/RecentFiles.h"

#include <QSettings>

namespace {
constexpr char kKey[] = "recentFiles";
}

QStringList RecentFiles::load() {
    QSettings s;
    QStringList list = s.value(kKey).toStringList();
    if (list.size() > kMaxRecent) list = list.mid(0, kMaxRecent);
    return list;
}

void RecentFiles::add(const QString &absPath) {
    if (absPath.isEmpty()) return;
    QStringList list = load();
    list.removeAll(absPath);
    list.prepend(absPath);
    if (list.size() > kMaxRecent) list = list.mid(0, kMaxRecent);
    QSettings s;
    s.setValue(kKey, list);
}

void RecentFiles::remove(const QString &absPath) {
    QStringList list = load();
    if (list.removeAll(absPath) > 0) {
        QSettings s;
        s.setValue(kKey, list);
    }
}
