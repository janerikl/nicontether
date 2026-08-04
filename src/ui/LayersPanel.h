#pragma once

#include <QByteArray>
#include <QIcon>
#include <QImage>
#include <QPair>
#include <QSet>
#include <QVector>
#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
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

    // previewImage is the tab's current composited render, used only for the
    // pinned Background row's thumbnail; passing a null image leaves
    // whatever thumbnail is already cached (e.g. between renders) in place.
    void setMasks(const QVector<Mask> &masks, int activeIndex, bool hasBackground,
                  bool backgroundHidden = false, const QImage &previewImage = QImage());
    // Remove Object section: a flat list of Adjustments::removals, one row
    // per content-aware fill (visibility checkbox + Delete button), mirroring
    // the Shapes section's per-row eye toggle. Top of the list = most
    // recently created removal.
    void setRemovals(const QVector<RemoveObjectOp> &removals, int activeIndex);
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
    // type defaults to MaskType::None (a plain adjustment layer, the
    // previous behaviour of the panel's single "Add Layer" action) so
    // existing single-arg emitters/connections keep working unchanged.
    void addMaskRequested(MaskType type = MaskType::None);
    // Shape/TextBox creation has no prior signal to reuse (the Add Layer
    // menu is the first way to create one outside the canvas), so it gets
    // its own signal. shapeType is only meaningful when type == Shape.
    void addLayerRequested(MaskType type, ShapeType shapeType = ShapeType::Rectangle);
    void addImageLayerRequested(const QString &path); // "Add Image Layer…" chosen a file
    void selectMaskRequested(int index);
    void deleteMaskRequested();
    void duplicateMaskRequested();
    void maskAdjustChanged(const MaskAdjust &a);
    void maskOpacityChanged(double opacity); // 0..1
    void maskBlendChanged(BlendMode mode);
    void maskImageTransformChanged(double offsetX, double offsetY, double scaleX,
                                   double scaleY, bool lockRatio);
    void maskVisibleChanged(int index, bool visible);
    void maskNameChanged(const QString &name);
    void maskRenamed(int index, const QString &name); // inline rename of any row in the list
    // Full new order of masks() indices, plus any indices whose drag pulled
    // them out of a group's nested rows (their groupId should be cleared).
    void maskReorderRequested(const QVector<int> &newOrder, const QVector<int> &leftGroupIndices,
                               const QVector<QPair<int, QString>> &joinGroups);
    void groupMasksRequested(const QVector<int> &indices);
    void ungroupMasksRequested(const QVector<int> &indices);
    void maskTypeChanged(MaskType type);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);
    void maskTextChanged(const QString &text, const QString &family, double pixelSize,
                         bool bold, bool italic);

    void selectRemovalRequested(int index);
    void removalVisibleChanged(int index, bool visible);
    void deleteRemovalRequested(int index);

private:
    void emitAdjust();
    void emitImageTransform();
    void loadActive();
    void rebuildList();
    void doRebuildList();
    void updateCurrentItemHighlight();
    bool masksContentEqual(const QVector<Mask> &masks, bool hasBackground, bool backgroundHidden) const;
    void rebuildRemovalList();

    // Row thumbnails (see doRebuildList()). Each returns an icon sized to
    // m_maskList's iconSize().
    QIcon maskThumbnail(const Mask &m) const;
    QIcon groupThumbnail() const;
    QIcon backgroundThumbnail() const;

    QVector<Mask> m_masks;
    int m_active = -1;
    bool m_hasBackground = false;
    bool m_backgroundHidden = false;
    QVector<RemoveObjectOp> m_removals;
    int m_activeRemoval = -1;
    bool m_syncing = false;
    // True while a coalesced doRebuildList() call is pending on the event
    // loop (see rebuildList()).
    bool m_rebuildScheduled = false;
    // Cached for the pinned Background row's thumbnail; updated opportunistically
    // by setMasks() (see its previewImage parameter) without forcing a rebuild.
    QImage m_backgroundPreview;
    MaskAdjust m_curAdjust; // last-loaded active layer's adjustment, patched per-section

    QTreeWidget *m_maskList = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_delete = nullptr;
    QPushButton *m_groupMasks = nullptr;
    QPushButton *m_ungroupMasks = nullptr;
    // groupId -> user collapsed it, so rebuildList() can restore collapse
    // state instead of always expanding every group.
    QSet<QString> m_collapsedMaskGroups;

    QLineEdit *m_name = nullptr;
    QSlider *m_opacity = nullptr;
    QComboBox *m_blend = nullptr;
    QSlider *m_imagePosX = nullptr;
    QSlider *m_imagePosY = nullptr;
    QSlider *m_imageScaleX = nullptr;
    QSlider *m_imageScaleY = nullptr;
    QCheckBox *m_imageLockRatio = nullptr;
    // Wraps the "Image Layer" header + Position/Scale/Lock-ratio form so the
    // whole section can be hidden (not just disabled) when the selected
    // layer isn't an image layer — e.g. a Text/Shape layer, which have no
    // use for these fields at all.
    QWidget *m_imageSection = nullptr;

    QMainWindow *m_inner = nullptr; // hosts the six per-section docks

    TonePanel *m_tonePanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    MaskPanel *m_maskPanel = nullptr;

    QListWidget *m_removalList = nullptr;
    QPushButton *m_deleteRemoval = nullptr;

    QDockWidget *m_toneSectionDock = nullptr;
    QDockWidget *m_colorSectionDock = nullptr;
    QDockWidget *m_toneCurveSectionDock = nullptr;
    QDockWidget *m_levelsSectionDock = nullptr;
    QDockWidget *m_detailEffectsSectionDock = nullptr;
    QDockWidget *m_masksSectionDock = nullptr;
    QDockWidget *m_removalsSectionDock = nullptr;
};
