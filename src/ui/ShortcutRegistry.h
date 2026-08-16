#pragma once

#include <QKeySequence>
#include <QShortcut>
#include <QString>
#include <QVector>
#include <functional>
#include <type_traits>

// Central registry of rebindable keyboard shortcuts. Any QAction/QShortcut/
// QToolButton that owns a shortcut registers itself here (they all share the
// same shortcut()/setShortcut(QKeySequence) interface, wrapped behind
// std::function so the registry doesn't need to know the concrete type).
// Rebinding persists overrides to QSettings under "shortcuts/<id>", read back
// and applied the next time that id is registered (i.e. on next launch).
class ShortcutRegistry {
public:
    struct Entry {
        QString id;
        QString category;
        QString label;
        QKeySequence defaultSeq;
        std::function<QKeySequence()> get;
        std::function<void(const QKeySequence &)> set;
    };

    static ShortcutRegistry &instance();

    template <typename T>
    void registerShortcut(const QString &id, const QString &category,
                           const QString &label, T *target,
                           const QKeySequence &defaultSeq) {
        Entry e;
        e.id = id;
        e.category = category;
        e.label = label;
        e.defaultSeq = defaultSeq;
        if constexpr (std::is_same_v<T, QShortcut>) {
            // QShortcut uses key()/setKey() instead of shortcut()/setShortcut().
            e.get = [target] { return target->key(); };
            e.set = [target](const QKeySequence &seq) { target->setKey(seq); };
        } else {
            e.get = [target] { return target->shortcut(); };
            e.set = [target](const QKeySequence &seq) { target->setShortcut(seq); };
        }
        addEntry(std::move(e), target);
    }

    const QVector<Entry> &entries() const { return m_entries; }

    // Applies seq to the binding and persists it. Clears any other binding
    // currently holding the same non-empty sequence.
    void rebind(const QString &id, const QKeySequence &seq);
    void resetToDefault(const QString &id);
    void resetAllToDefaults();

    // Returns the id currently bound to seq, or empty if free/unbound.
    QString idBoundTo(const QKeySequence &seq) const;

private:
    ShortcutRegistry() = default;
    void addEntry(Entry &&e, void *target);
    int indexOf(const QString &id) const;
    void applyPersistedOrDefault(Entry &e);

    QVector<Entry> m_entries;
};
