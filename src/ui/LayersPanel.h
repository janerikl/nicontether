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

// The layer stack: a full-height list (drag to reorder, eye icon to toggle
// visibility, Add/Duplicate/Delete) plus, for the selected layer, its name,
// opacity, and blend mode. Tone/Colour/Tone Curve/Levels/Detail & Effects
// adjustments and mask shape editing each live in their own dock (TonePanel,
// ColorPanel, ToneCurvePanel, the per-layer LevelsPanel, DetailEffectsPanel,
// MaskPanel) — see RetouchWindow::refreshMaskPanel(), which keeps all of them
// in sync with the selected layer. Purely a view — it emits intent signals
// and is refreshed via setMasks(); RetouchWindow routes to the tab.
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
    void maskOpacityChanged(double opacity); // 0..1
    void maskBlendChanged(BlendMode mode);
    void maskVisibleChanged(int index, bool visible);
    void maskNameChanged(const QString &name);
    void maskReorderRequested(int from, int to);

private:
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
};
