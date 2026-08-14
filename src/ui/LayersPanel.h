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
class TonePanel;
class ColorPanel;
class ToneCurvePanel;
class LevelsPanel;
class DetailEffectsPanel;
class MaskPanel;
class LayerAdjustmentsPanel;

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, blend mode, and the *complete* tone/colour/curve/levels/detail/mask
// editing surface. The per-section editing widgets (Tone, Colour, Tone
// Curve, Levels, Detail & Effects, Masks, Remove Object) live in a separate
// LayerAdjustmentsPanel (one at a time, via a QStackedWidget) — this class
// only owns the layer list/name/opacity/blend controls and wires the
// LayerAdjustmentsPanel's widgets (via setAdjustmentsPanel()) into the same
// signals it always emitted. Right-clicking the layer list opens a context
// menu with one entry per section (see sectionRequested); RetouchWindow
// routes that to opening/switching the LayerAdjustmentsPanel dock. Purely a
// view — it emits intent signals and is refreshed via setMasks();
// RetouchWindow routes to the tab.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    // Wires this panel up to the (externally owned, separately docked)
    // LayerAdjustmentsPanel's section widgets. Must be called once, before
    // setMasks() is first used to populate them.
    void setAdjustmentsPanel(LayerAdjustmentsPanel *panel);

    // The Background layer (the tab's own base photo) is just a normal
    // MaskType::Background entry in `masks` now — no separate pinned row or
    // extra parameters, same as every other layer.
    void setMasks(const QVector<Mask> &masks, int activeIndex);
    // Persisted per-group state (opacity/visibility/blend/collapsed), keyed
    // by Mask::groupId — see MaskGroup in Adjustments.h. Independent of
    // setMasks() so a group-properties-only change doesn't need to touch the
    // (much larger, order-sensitive) mask list diffing.
    void setGroups(const QVector<MaskGroup> &groups);
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
    // Current tab's image width in px, so the mask panel can show the brush
    // size slider's equivalent pixel value.
    void setImageWidth(int width);

    // Original masks() indices of every currently-selected row in the layer
    // list (group parent rows contribute nothing of their own). Used by
    // RetouchWindow's Ctrl+G shortcut to tell a multi-layer-selection group
    // apart from a canvas shape-selection group.
    QVector<int> selectedMaskIndices() const;

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
    void addSvgLayerRequested(const QString &path); // "Add SVG Layer…" chosen a file
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
    void groupRenamed(const QString &groupId, const QString &name); // inline rename of a group header row
    void maskTypeChanged(MaskType type);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);
    void maskTextChanged(const QString &text, const QString &family, double pixelSize,
                         bool bold, bool italic);
    void gradientFillChanged(bool enabled, const QColor &colorA, const QColor &colorB);
    // Edited via the same Opacity/Blend controls as a mask, retargeted while
    // a group header is the current tree selection (see m_selectedGroupId).
    void groupPropertiesChanged(const QString &groupId, double opacity, bool visible,
                                BlendMode blend);

    void selectRemovalRequested(int index);
    void removalVisibleChanged(int index, bool visible);
    void deleteRemovalRequested(int index);

    // A context-menu entry on the layer list was picked; `section` is a
    // LayerAdjustmentsPanel::Section value. RetouchWindow opens/switches the
    // LayerAdjustmentsPanel dock to it.
    void sectionRequested(int section);

private:
    void emitAdjust();
    void emitImageTransform();
    void loadActive();
    void rebuildList();
    void doRebuildList();
    void updateCurrentItemHighlight();
    bool masksContentEqual(const QVector<Mask> &masks) const;
    void rebuildRemovalList();

    // Row thumbnails (see doRebuildList()). Each returns an icon sized to
    // m_maskList's iconSize().
    QIcon maskThumbnail(const Mask &m) const;
    QIcon groupThumbnail() const;

    QVector<Mask> m_masks;
    QVector<MaskGroup> m_groups;
    // Non-empty when the current tree selection is a group header (not a
    // member mask) — set in currentItemChanged, read by loadActive() to
    // retarget the Opacity/Blend controls at the group instead of a mask.
    QString m_selectedGroupId;
    int m_active = -1;
    QVector<RemoveObjectOp> m_removals;
    int m_activeRemoval = -1;
    bool m_syncing = false;
    // True while a coalesced doRebuildList() call is pending on the event
    // loop (see rebuildList()).
    bool m_rebuildScheduled = false;
    // True from the moment a drop's reorder is computed until the deferred
    // maskReorderRequested emission it queued actually fires. Qt's own
    // drag-drop machinery can invoke MaskTreeWidget::dropEvent more than
    // once for what is, from the user's perspective, a single drag -- each
    // invocation reads the same (unchanged) tree and computes the same
    // permutation, so without this guard every extra invocation queues
    // another application of that permutation. A transposition is its own
    // inverse, so a second application on top of the first silently reverts
    // it, which looks like the drag simply didn't stick.
    bool m_reorderPending = false;
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

    // Non-owning: these live in the externally-owned LayerAdjustmentsPanel,
    // wired up here in setAdjustmentsPanel().
    TonePanel *m_tonePanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    MaskPanel *m_maskPanel = nullptr;

    QListWidget *m_removalList = nullptr;
    QPushButton *m_deleteRemoval = nullptr;
};
