#pragma once

#include <QToolButton>

#include "edit/BrushPreset.h"

class QMenu;

// A "Presets" menu button for brush size/hardness, used by both MaskPanel
// (editing a Brush-type mask) and RetouchWindow's brush tool options row
// (editing a Paint-type mask). Owns its own BrushPresetStore and reloads
// from QSettings each time the menu opens, so both call sites stay in sync
// without needing a shared instance or cross-widget signals.
class BrushPresetMenuButton : public QToolButton {
    Q_OBJECT
public:
    explicit BrushPresetMenuButton(QWidget *parent = nullptr);

    // Call before the menu opens so "Save Current as Preset…" captures the
    // host's live slider state.
    void setCurrentValues(double brushRadius, double hardness);

signals:
    void presetApplied(double brushRadius, double hardness);

private:
    void rebuildMenu();
    void onSavePreset();
    void onDeletePreset();

    QMenu *m_menu = nullptr;
    BrushPresetStore m_store;
    double m_currentRadius = 0.06;
    double m_currentHardness = 0.5;
};
