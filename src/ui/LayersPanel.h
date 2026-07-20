#pragma once

#include <QByteArray>
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
class QMainWindow;
class QDockWidget;
class TonePanel;
class ColorPanel;
class ToneCurvePanel;
class LevelsPanel;
class DetailEffectsPanel;
class MaskPanel;

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, blend mode, and the *complete* tone/colour/curve/levels/detail/mask
// editing surface. Each section (Tone, Colour, Tone Curve, Levels, Detail &
// Effects, Masks) is its own QDockWidget nested inside a small inner
// QMainWindow — so each has a real title bar with collapse/close controls,
// while the whole assembly still docks/floats as one "Layers"
// panel from RetouchWindow's point of view. Purely a view — it emits intent
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

    // The six per-section dock widgets, for RetouchWindow to expose reopen
    // actions (toggleViewAction()) in a View menu submenu.
    QVector<QDockWidget *> sectionDocks() const;

    // The inner QMainWindow's dock layout (which section is closed, its
    // geometry) isn't covered by RetouchWindow's own saveState()/
    // restoreState() — that only sees docks added directly to it, not ones
    // nested inside this widget's inner QMainWindow. RetouchWindow persists
    // these explicitly via its own QSettings entry, mirroring how it
    // persists its own window/state.
    QByteArray innerDockState() const;
    void restoreInnerDockState(const QByteArray &state);
    // Show all six sections (used by View > Reset Panels).
    void resetSections();

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

    QMainWindow *m_inner = nullptr; // hosts the six per-section docks

    TonePanel *m_tonePanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    MaskPanel *m_maskPanel = nullptr;

    QDockWidget *m_toneSectionDock = nullptr;
    QDockWidget *m_colorSectionDock = nullptr;
    QDockWidget *m_toneCurveSectionDock = nullptr;
    QDockWidget *m_levelsSectionDock = nullptr;
    QDockWidget *m_detailEffectsSectionDock = nullptr;
    QDockWidget *m_masksSectionDock = nullptr;
};
