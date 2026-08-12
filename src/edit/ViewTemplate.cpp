#include "edit/ViewTemplate.h"

#include <QSettings>

bool ViewTemplateStore::isCustom(const QString &name) const {
    for (const ViewTemplate &t : m_custom)
        if (t.name == name) return true;
    return false;
}

void ViewTemplateStore::addOrUpdate(const ViewTemplate &t) {
    ViewTemplate entry = t;
    entry.builtIn = false;
    for (ViewTemplate &e : m_custom) {
        if (e.name == entry.name) {
            e = entry;
            save();
            return;
        }
    }
    m_custom.append(entry);
    save();
}

void ViewTemplateStore::remove(const QString &name) {
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].name == name) {
            m_custom.removeAt(i);
            save();
            return;
        }
    }
}

void ViewTemplateStore::rename(const QString &oldName, const QString &newName) {
    for (ViewTemplate &e : m_custom) {
        if (e.name == oldName) {
            e.name = newName;
            save();
            return;
        }
    }
}

void ViewTemplateStore::load() {
    m_custom.clear();
    QSettings s;
    int n = s.beginReadArray("viewTemplates");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        ViewTemplate t;
        t.name = s.value("name").toString();
        t.state = s.value("state").toByteArray();
        t.filmstripVisible = s.value("filmstripVisible", true).toBool();
        t.rulersVisible = s.value("rulersVisible", false).toBool();
        t.builtIn = false;
        if (!t.name.isEmpty()) m_custom.append(t);
    }
    s.endArray();
}

void ViewTemplateStore::save() const {
    QSettings s;
    s.beginWriteArray("viewTemplates");
    for (int i = 0; i < m_custom.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("name", m_custom[i].name);
        s.setValue("state", m_custom[i].state);
        s.setValue("filmstripVisible", m_custom[i].filmstripVisible);
        s.setValue("rulersVisible", m_custom[i].rulersVisible);
    }
    s.endArray();
}
