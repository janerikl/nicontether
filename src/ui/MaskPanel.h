#pragma once

#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QCheckBox;
class QSlider;
class QLabel;

// Mask editor for whichever layer is selected in the Layers panel: a type
// combo (None / Radial / Graduated / Brush) to add, remove, or change the
// layer's mask, plus its shape controls (invert / feather / brush hardness &
// size / auto mask). Separate from LayersPanel — this dock is purely about
// *where* a layer's adjustment applies, not the adjustment itself. Purely a
// view — it emits intent signals and is refreshed via setMask(); RetouchWindow
// routes to the tab.
class MaskPanel : public QWidget {
    Q_OBJECT
public:
    explicit MaskPanel(QWidget *parent = nullptr);

    void setMask(const Mask &mask, bool hasSelection);
    void clear();
    // Reflect a brush radius change (e.g. ctrl+wheel on the canvas) without
    // a full resync.
    void setBrushRadius(double radiusNorm);

signals:
    void maskTypeChanged(MaskType type);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);

private:
    void emitShape();
    void loadMask();

    Mask m_mask;
    bool m_hasSelection = false;
    bool m_syncing = false;

    QLabel *m_hint = nullptr;
    QComboBox *m_type = nullptr;

    QCheckBox *m_invert = nullptr;
    QSlider *m_feather = nullptr;
    QLabel *m_hardnessLabel = nullptr;
    QSlider *m_hardness = nullptr;
    QLabel *m_brushSizeLabel = nullptr;
    QSlider *m_brushSize = nullptr;
    QCheckBox *m_autoMask = nullptr;
};
