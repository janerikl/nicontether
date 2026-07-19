#pragma once

#include <QVector>
#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;
class CurveEditor;
class LevelsPanel;

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, blend mode, and the *complete* tone/colour/curve/levels/detail
// adjustment set — the same power as the main Adjustments dock, scoped to
// this layer. Mask shape editing (type/invert/feather/brush) lives in the
// separate MaskPanel dock; new layers start unmasked (whole image) and a mask
// can be added there afterward. Purely a view — it emits intent signals and
// is refreshed via setMasks(); RetouchWindow routes to the tab.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setMasks(const QVector<Mask> &masks, int activeIndex);
    void clear();

signals:
    void addMaskRequested();
    void addImageLayerRequested(const QString &path); // "Add Image Layer…" chosen a file
    void selectMaskRequested(int index);
    void deleteMaskRequested();
    void duplicateMaskRequested();
    void maskAdjustChanged(const MaskAdjust &a);
    void maskOpacityChanged(double opacity); // 0..1
    void maskBlendChanged(BlendMode mode);
    void maskVisibleChanged(int index, bool visible);
    void maskNameChanged(const QString &name);
    void maskReorderRequested(int from, int to);

private:
    void emitAdjust();
    void loadActive();
    void rebuildList();

    QVector<Mask> m_masks;
    int m_active = -1;
    bool m_syncing = false;

    QListWidget *m_maskList = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_delete = nullptr;

    QLineEdit *m_name = nullptr;
    QSlider *m_opacity = nullptr;
    QComboBox *m_blend = nullptr;

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
