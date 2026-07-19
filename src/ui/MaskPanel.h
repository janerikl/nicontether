#pragma once

#include <QVector>
#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QCheckBox;
class QSlider;
class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;
class CurveEditor;
class LevelsPanel;

// Layers panel: a real Lightroom/Photoshop-style layer stack. A full-height
// list at top (drag to reorder, eye icon to toggle visibility, Duplicate/
// Delete), and below it the *complete* editing surface for whichever layer is
// selected — name, opacity, blend mode, its mask shape (invert / feather /
// brush hardness & size), and the full tone/colour/curve/levels/detail
// adjustment set (the same power as the main Adjustments dock, scoped to this
// layer). Layers are added from the sidebar tool's flyout, not here. Purely a
// view — it emits intent signals and is refreshed via setMasks(); RetouchWindow
// routes to the tab.
class MaskPanel : public QWidget {
    Q_OBJECT
public:
    explicit MaskPanel(QWidget *parent = nullptr);

    void setMasks(const QVector<Mask> &masks, int activeIndex);
    void clear();
    // Reflect a brush radius change (e.g. ctrl+wheel on the canvas) without
    // rebuilding the whole layer list.
    void setBrushRadius(double radiusNorm);

signals:
    void selectMaskRequested(int index);
    void deleteMaskRequested();
    void duplicateMaskRequested();
    void maskAdjustChanged(const MaskAdjust &a);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);
    void maskOpacityChanged(double opacity); // 0..1
    void maskBlendChanged(BlendMode mode);
    void maskVisibleChanged(bool visible);
    void maskNameChanged(const QString &name);
    void maskReorderRequested(int from, int to);

private:
    void emitAdjust();
    void emitShape();
    void loadActive();
    void rebuildList();

    QVector<Mask> m_masks;
    int m_active = -1;
    bool m_syncing = false;

    QListWidget *m_maskList = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_delete = nullptr;
    QLabel *m_hint = nullptr;

    QLineEdit *m_name = nullptr;
    QSlider *m_opacity = nullptr;
    QComboBox *m_blend = nullptr;

    QCheckBox *m_invert = nullptr;
    QSlider *m_feather = nullptr;
    QLabel *m_hardnessLabel = nullptr;
    QSlider *m_hardness = nullptr;
    QLabel *m_brushSizeLabel = nullptr;
    QSlider *m_brushSize = nullptr;
    QCheckBox *m_autoMask = nullptr;

    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_highlights = nullptr;
    QSlider *m_shadows = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_vibrance = nullptr;
    QSlider *m_temperature = nullptr;
    QSlider *m_tint = nullptr;

    CurveEditor *m_curve = nullptr;
    LevelsPanel *m_levels = nullptr;
    Levels m_curLevels;   // last value LevelsPanel reported (it has no getter)
    QSlider *m_clarity = nullptr;
    QSlider *m_sharpen = nullptr;
    QSlider *m_vignette = nullptr;
};
