#pragma once

#include <QList>
#include <QString>

#include "edit/Adjustments.h"

// A named "develop preset": a snapshot of the portable (image-independent)
// tone/colour/detail fields from Adjustments — the same field set
// RetouchWindow::mergePortable copies for Copy/Paste Edits. Geometry, masks,
// heals, text/shapes and removals are never part of a preset since they're
// tied to a specific photo.
struct AdjustmentPreset {
    QString name;
    Adjustments adj; // only the portable fields (see mergePortable) matter
    bool builtIn = false;
};

// Persists custom presets via QSettings and exposes them alongside the
// non-editable built-in templates. Mirrors ExportPresetStore.
class AdjustmentPresetStore {
public:
    AdjustmentPresetStore() { load(); }

    static QList<AdjustmentPreset> builtins();
    QList<AdjustmentPreset> all() const; // builtins + custom
    const QList<AdjustmentPreset> &custom() const { return m_custom; }
    bool isCustom(const QString &name) const;

    void addOrUpdate(const AdjustmentPreset &p); // by name; persists
    void remove(const QString &name);            // custom only; persists

private:
    void load();
    void save() const;

    QList<AdjustmentPreset> m_custom;
};
