#pragma once

#include <QList>
#include <QString>

// A named brush size/hardness combo, analogous to AdjustmentPreset but for
// the brush/paint tool. Only the two params the brush model currently has
// (size, hardness) are stored -- no opacity/spacing/smoothing fields exist
// to save yet.
struct BrushPreset {
    QString name;
    double brushRadius = 0.06; // 0..1, normalized to image width (same convention as Mask::brushRadius)
    double hardness = 0.5;     // 0..1
    bool builtIn = false;
};

// Persists custom brush presets via QSettings and exposes them alongside the
// non-editable built-in templates. Mirrors AdjustmentPresetStore/ExportPresetStore.
class BrushPresetStore {
public:
    BrushPresetStore() { load(); }

    static QList<BrushPreset> builtins();
    QList<BrushPreset> all() const; // builtins + custom
    const QList<BrushPreset> &custom() const { return m_custom; }
    bool isCustom(const QString &name) const;

    void addOrUpdate(const BrushPreset &p); // by name; persists
    void remove(const QString &name);       // custom only; persists

private:
    void load();
    void save() const;

    QList<BrushPreset> m_custom;
};
