#pragma once

#include <QString>
#include <QStringList>

// Small, UI-independent store for recently opened individual photo files.
// Backed by QSettings (the existing "Photonloom" org/app scope). The list
// holds absolute file paths, newest first, de-duplicated by path and capped
// at kMaxRecent.
class RecentFiles {
public:
    static constexpr int kMaxRecent = 10;

    // Read the capped list from QSettings (newest first).
    static QStringList load();
    // Insert absPath at the front, de-dup by path, cap to kMaxRecent, persist.
    static void add(const QString &absPath);
    // Drop absPath from the list, persist.
    static void remove(const QString &absPath);
};
