#include "ui/ShortcutRegistry.h"

#include <QSettings>

ShortcutRegistry &ShortcutRegistry::instance() {
    static ShortcutRegistry reg;
    return reg;
}

int ShortcutRegistry::indexOf(const QString &id) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id) return i;
    return -1;
}

void ShortcutRegistry::applyPersistedOrDefault(Entry &e) {
    QSettings s;
    const QString key = QString("shortcuts/%1").arg(e.id);
    if (s.contains(key)) {
        e.set(QKeySequence::fromString(s.value(key).toString()));
    } else {
        e.set(e.defaultSeq);
    }
}

void ShortcutRegistry::addEntry(Entry &&e, void * /*target*/) {
    int idx = indexOf(e.id);
    if (idx >= 0) {
        m_entries[idx] = std::move(e);
        applyPersistedOrDefault(m_entries[idx]);
        return;
    }
    m_entries.push_back(std::move(e));
    applyPersistedOrDefault(m_entries.back());
}

void ShortcutRegistry::rebind(const QString &id, const QKeySequence &seq) {
    int idx = indexOf(id);
    if (idx < 0) return;

    if (!seq.isEmpty()) {
        const QString otherId = idBoundTo(seq);
        if (!otherId.isEmpty() && otherId != id) {
            int otherIdx = indexOf(otherId);
            if (otherIdx >= 0) {
                m_entries[otherIdx].set(QKeySequence());
                QSettings s;
                s.setValue(QString("shortcuts/%1").arg(otherId), QString());
            }
        }
    }

    m_entries[idx].set(seq);
    QSettings s;
    s.setValue(QString("shortcuts/%1").arg(id), seq.toString());
}

void ShortcutRegistry::resetToDefault(const QString &id) {
    int idx = indexOf(id);
    if (idx < 0) return;
    m_entries[idx].set(m_entries[idx].defaultSeq);
    QSettings s;
    s.remove(QString("shortcuts/%1").arg(id));
}

void ShortcutRegistry::resetAllToDefaults() {
    for (Entry &e : m_entries) {
        e.set(e.defaultSeq);
        QSettings s;
        s.remove(QString("shortcuts/%1").arg(e.id));
    }
}

QString ShortcutRegistry::idBoundTo(const QKeySequence &seq) const {
    if (seq.isEmpty()) return QString();
    for (const Entry &e : m_entries)
        if (!e.get().isEmpty() && e.get() == seq) return e.id;
    return QString();
}
