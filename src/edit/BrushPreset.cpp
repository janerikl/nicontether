#include "edit/BrushPreset.h"

#include <QSettings>

QList<BrushPreset> BrushPresetStore::builtins() {
    QList<BrushPreset> b;
    auto add = [&](const QString &name, double size, double hardness) {
        BrushPreset p;
        p.name = name;
        p.brushRadius = size / 100.0;
        p.hardness = hardness / 100.0;
        p.builtIn = true;
        b.append(p);
    };

    // size is on the same 1-40 "% of image width" scale as m_brushSize/m_paintSize;
    // hardness is 0-100.
    add("Small Hard", 4, 100);
    add("Small Soft", 4, 40);
    add("Medium Hard", 14, 100);
    add("Medium Soft", 14, 40);
    add("Large Hard", 28, 90);
    add("Large Soft", 28, 25);

    return b;
}

QList<BrushPreset> BrushPresetStore::all() const {
    return builtins() + m_custom;
}

bool BrushPresetStore::isCustom(const QString &name) const {
    for (const BrushPreset &p : m_custom)
        if (p.name == name) return true;
    return false;
}

void BrushPresetStore::addOrUpdate(const BrushPreset &p) {
    BrushPreset entry = p;
    entry.builtIn = false;
    for (BrushPreset &e : m_custom) {
        if (e.name == entry.name) {
            e = entry;
            save();
            return;
        }
    }
    m_custom.append(entry);
    save();
}

void BrushPresetStore::remove(const QString &name) {
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].name == name) {
            m_custom.removeAt(i);
            save();
            return;
        }
    }
}

void BrushPresetStore::load() {
    m_custom.clear();
    QSettings s;
    int n = s.beginReadArray("brushPresets");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        BrushPreset p;
        p.name = s.value("name").toString();
        p.builtIn = false;
        p.brushRadius = s.value("brushRadius", 0.06).toDouble();
        p.hardness = s.value("hardness", 0.5).toDouble();
        if (!p.name.isEmpty()) m_custom.append(p);
    }
    s.endArray();
}

void BrushPresetStore::save() const {
    QSettings s;
    s.beginWriteArray("brushPresets");
    for (int i = 0; i < m_custom.size(); ++i) {
        s.setArrayIndex(i);
        const BrushPreset &p = m_custom[i];
        s.setValue("name", p.name);
        s.setValue("brushRadius", p.brushRadius);
        s.setValue("hardness", p.hardness);
    }
    s.endArray();
}
