#include "capture/SessionManager.h"

#include <QDir>
#include <QStandardPaths>
#include <QDate>
#include <QRegularExpression>

SessionManager::SessionManager() {
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pics.isEmpty()) pics = QDir::homePath();
    m_base = QDir(pics).filePath("Tether");
}

QString SessionManager::startSession(const QString &name) {
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    if (safe.isEmpty()) safe = "session";

    QString folder = QString("%1_%2")
                         .arg(QDate::currentDate().toString("yyyy-MM-dd"))
                         .arg(safe);
    m_dir = QDir(m_base).filePath(folder);
    QDir().mkpath(m_dir);
    return m_dir;
}
