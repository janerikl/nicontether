#include "ui/LayersPanel.h"
#include "edit/CurveEditor.h"
#include "ui/LevelsPanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString maskTypeLabel(MaskType t) {
    switch (t) {
    case MaskType::Radial: return "Radial";
    case MaskType::Linear: return "Graduated";
    case MaskType::Brush:  return "Brush";
    case MaskType::None:   return "Layer";
    }
    return "Layer";
}
} // namespace

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Layer list: checkable (visibility) rows, drag to reorder, Duplicate/Delete.
    auto *selRow = new QHBoxLayout;
    m_maskList = new QListWidget;
    m_maskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_maskList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_maskList->setMinimumHeight(140);
    selRow->addWidget(m_maskList, 1);
    auto *listButtons = new QVBoxLayout;
    m_add = new QPushButton("Add");
    m_duplicate = new QPushButton("Duplicate");
    m_delete = new QPushButton("Delete");
    listButtons->addWidget(m_add);
    listButtons->addWidget(m_duplicate);
    listButtons->addWidget(m_delete);
    listButtons->addStretch(1);
    selRow->addLayout(listButtons);
    root->addLayout(selRow, 1);

    // Everything below is the full editing surface for the selected layer —
    // scrollable, since it carries the same weight as the main Adjustments dock.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *editArea = new QWidget;
    auto *edit = new QVBoxLayout(editArea);
    edit->setContentsMargins(0, 0, 0, 0);
    edit->setSpacing(6);

    // Name / opacity / blend mode.
    auto *props = new QFormLayout;
    m_name = new QLineEdit;
    props->addRow("Name:", m_name);
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    props->addRow("Opacity:", m_opacity);
    m_blend = new QComboBox;
    m_blend->addItem("Normal", int(BlendMode::Normal));
    m_blend->addItem("Multiply", int(BlendMode::Multiply));
    m_blend->addItem("Screen", int(BlendMode::Screen));
    m_blend->addItem("Overlay", int(BlendMode::Overlay));
    m_blend->addItem("Soft Light", int(BlendMode::SoftLight));
    props->addRow("Blend:", m_blend);
    edit->addLayout(props);

    // Tone/colour sliders — the same set as the main Adjustments dock.
    edit->addWidget(new QLabel("<b>Tone</b>"));
    auto *toneForm = new QFormLayout;
    auto mk = [&](QFormLayout *form, const QString &label, int lo = -100, int hi = 100) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitAdjust(); });
        return s;
    };
    m_brightness = mk(toneForm, "Brightness");
    m_contrast = mk(toneForm, "Contrast");
    m_highlights = mk(toneForm, "Highlights");
    m_shadows = mk(toneForm, "Shadows");
    edit->addLayout(toneForm);

    edit->addWidget(new QLabel("<b>Colour</b>"));
    auto *colForm = new QFormLayout;
    m_saturation = mk(colForm, "Saturation");
    m_vibrance = mk(colForm, "Vibrance");
    m_temperature = mk(colForm, "Temperature");
    m_tint = mk(colForm, "Tint (green/magenta)");
    edit->addLayout(colForm);

    edit->addWidget(new QLabel("<b>Tone Curve</b>"));
    m_curve = new CurveEditor;
    edit->addWidget(m_curve);
    connect(m_curve, &CurveEditor::curveChanged, this,
            [this](const QVector<QPointF> &) { emitAdjust(); });

    edit->addWidget(new QLabel("<b>Levels</b>"));
    m_levels = new LevelsPanel;
    edit->addWidget(m_levels);
    connect(m_levels, &LevelsPanel::levelsChanged, this,
            [this](const Levels &lv) {
                m_curLevels = lv;
                emitAdjust();
            });

    edit->addWidget(new QLabel("<b>Detail &amp; Effects</b>"));
    auto *fxForm = new QFormLayout;
    m_clarity = mk(fxForm, "Clarity");
    m_sharpen = mk(fxForm, "Sharpen", 0, 100);
    m_vignette = mk(fxForm, "Vignette");
    edit->addLayout(fxForm);
    edit->addStretch(1);

    scroll->setWidget(editArea);
    root->addWidget(scroll, 2);

    auto *addMenu = new QMenu(m_add);
    QAction *addLayerAction = addMenu->addAction("Add Layer");
    connect(addLayerAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(); });
    QAction *addImageAction = addMenu->addAction("Add Image Layer…");
    connect(addImageAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(
            this, "Add Image Layer", QString(),
            "Images (*.jpg *.jpeg *.png *.tif *.tiff *.nef *.NEF);;All Files (*)");
        if (!path.isEmpty()) emit addImageLayerRequested(path);
    });
    m_add->setMenu(addMenu);

    connect(m_duplicate, &QPushButton::clicked, this,
            [this] { emit duplicateMaskRequested(); });
    connect(m_delete, &QPushButton::clicked, this,
            [this] { emit deleteMaskRequested(); });
    connect(m_maskList, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!m_syncing) emit selectMaskRequested(i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                emit maskVisibleChanged(i, item->checkState() == Qt::Checked);
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &,
                   int destRow) {
                if (m_syncing) return;
                int to = destRow > start ? destRow - 1 : destRow;
                emit maskReorderRequested(start, to);
            });
    connect(m_name, &QLineEdit::editingFinished, this,
            [this] { if (!m_syncing) emit maskNameChanged(m_name->text()); });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing) emit maskOpacityChanged(v / 100.0);
    });
    connect(m_blend, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_syncing) return;
                emit maskBlendChanged(BlendMode(m_blend->currentData().toInt()));
            });

    clear();
}

void LayersPanel::clear() {
    m_masks.clear();
    m_active = -1;
    m_syncing = true;
    m_maskList->clear();
    m_syncing = false;
    setEnabled(false);
}

void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex) {
    m_masks = masks;
    m_active = activeIndex;
    setEnabled(true);
    rebuildList();
    loadActive();
}

void LayersPanel::rebuildList() {
    m_syncing = true;
    m_maskList->clear();
    for (int i = 0; i < m_masks.size(); ++i) {
        const Mask &m = m_masks[i];
        QString label = m.name.isEmpty()
                            ? QString("Layer %1 (%2)").arg(i + 1).arg(maskTypeLabel(m.type))
                            : m.name;
        if (m.isImageLayer()) {
            if (m.sourceMissing) label += " (missing)";
            else if (m.sourceImageCache.isNull()) label += " (loading…)";
        }
        auto *item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
        m_maskList->addItem(item);
    }
    if (m_active >= 0 && m_active < m_masks.size())
        m_maskList->setCurrentRow(m_active);
    m_syncing = false;
}

void LayersPanel::loadActive() {
    const bool has = m_active >= 0 && m_active < m_masks.size();
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_duplicate, m_delete, m_brightness,
             m_contrast, m_highlights, m_shadows, m_saturation, m_vibrance,
             m_temperature, m_tint, m_curve, m_levels, m_clarity, m_sharpen,
             m_vignette})
        w->setEnabled(has);
    if (has) {
        const Mask &m = m_masks[m_active];
        m_name->setText(m.name);
        m_opacity->setValue(int(std::lround(m.opacity * 100)));
        int blendIdx = m_blend->findData(int(m.blend));
        m_blend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);
        const MaskAdjust &a = m.adj;
        m_brightness->setValue(a.brightness);
        m_contrast->setValue(a.contrast);
        m_highlights->setValue(a.highlights);
        m_shadows->setValue(a.shadows);
        m_saturation->setValue(a.saturation);
        m_vibrance->setValue(a.vibrance);
        m_temperature->setValue(a.temperature);
        m_tint->setValue(a.tint);
        m_curve->setCurve(a.curve);
        m_curLevels = a.levels;
        m_levels->setLevels(a.levels);
        m_clarity->setValue(a.clarity);
        m_sharpen->setValue(a.sharpen);
        m_vignette->setValue(a.vignette);
    } else {
        m_curve->resetCurve();
        m_levels->clear();
    }
    m_syncing = false;
}

void LayersPanel::emitAdjust() {
    if (m_syncing) return;
    MaskAdjust a;
    a.brightness = m_brightness->value();
    a.contrast = m_contrast->value();
    a.highlights = m_highlights->value();
    a.shadows = m_shadows->value();
    a.saturation = m_saturation->value();
    a.vibrance = m_vibrance->value();
    a.temperature = m_temperature->value();
    a.tint = m_tint->value();
    a.clarity = m_clarity->value();
    a.sharpen = m_sharpen->value();
    a.vignette = m_vignette->value();
    a.curve = m_curve->curve();
    a.levels = m_curLevels;
    // White balance has no UI here yet; preserve whatever the active layer
    // already has (e.g. loaded from an older sidecar).
    if (m_active >= 0 && m_active < m_masks.size()) {
        a.wbR = m_masks[m_active].adj.wbR;
        a.wbG = m_masks[m_active].adj.wbG;
        a.wbB = m_masks[m_active].adj.wbB;
    }
    emit maskAdjustChanged(a);
}
