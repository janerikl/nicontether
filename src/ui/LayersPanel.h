#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;
class TonePanel;
class ColorPanel;
class ToneCurvePanel;
class LevelsPanel;
class DetailEffectsPanel;
class MaskPanel;

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, blend mode, and the *complete* tone/colour/curve/levels/detail/mask
// editing surface — each section built from its own standalone widget
// (TonePanel, ColorPanel, ToneCurvePanel, LevelsPanel, DetailEffectsPanel,
// MaskPanel) but embedded here as one scrollable stack, so the whole thing
// docks/floats as a single "Layers" panel. Purely a view — it emits intent
// signals and is refreshed via setMasks(); RetouchWindow routes to the tab.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setMasks(const QVector<Mask> &masks, int activeIndex);
    void clear();

    // Pushes a rendered preview into the selected layer's Levels histogram
    // (no-op if no layer is selected). RetouchWindow calls this whenever
    // RetouchTab::maskPreviewUpdated fires.
    void setLevelsPreviewImage(const QImage &img);
    // Reflect a brush radius change (e.g. ctrl+wheel on the canvas) without
    // a full resync.
    void setMaskBrushRadius(double radiusNorm);

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
    void maskTypeChanged(MaskType type);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);

private:
    void emitAdjust();
    void loadActive();
    void rebuildList();

    QVector<Mask> m_masks;
    int m_active = -1;
    bool m_syncing = false;
    MaskAdjust m_curAdjust; // last-loaded active layer's adjustment, patched per-section

    QListWidget *m_maskList = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_delete = nullptr;

    QLineEdit *m_name = nullptr;
    QSlider *m_opacity = nullptr;
    QComboBox *m_blend = nullptr;

    TonePanel *m_tonePanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    MaskPanel *m_maskPanel = nullptr;
};
