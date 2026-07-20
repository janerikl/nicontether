#include "ui/LayersPanel.h"

#include "ui/ColorPanel.h"
#include "ui/DetailEffectsPanel.h"
#include "ui/LevelsPanel.h"
#include "ui/MaskPanel.h"
#include "ui/ToneCurvePanel.h"
#include "ui/TonePanel.h"

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
    case MaskType::Paint:  return "Paint";
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

    edit->addWidget(new QLabel("<b>Tone</b>"));
    m_tonePanel = new TonePanel;
    edit->addWidget(m_tonePanel);

    edit->addWidget(new QLabel("<b>Colour</b>"));
    m_colorPanel = new ColorPanel;
    edit->addWidget(m_colorPanel);

    edit->addWidget(new QLabel("<b>Tone Curve</b>"));
    m_toneCurvePanel = new ToneCurvePanel;
    edit->addWidget(m_toneCurvePanel);

    edit->addWidget(new QLabel("<b>Levels</b>"));
    m_levelsPanel = new LevelsPanel;
    edit->addWidget(m_levelsPanel);

    edit->addWidget(new QLabel("<b>Detail &amp; Effects</b>"));
    m_detailEffectsPanel = new DetailEffectsPanel;
    edit->addWidget(m_detailEffectsPanel);

    edit->addWidget(new QLabel("<b>Mask</b>"));
    m_maskPanel = new MaskPanel;
    edit->addWidget(m_maskPanel);

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

    connect(m_tonePanel, &TonePanel::adjustChanged, this,
            [this](int brightness, int contrast, int highlights, int shadows) {
                m_curAdjust.brightness = brightness;
                m_curAdjust.contrast = contrast;
                m_curAdjust.highlights = highlights;
                m_curAdjust.shadows = shadows;
                emitAdjust();
            });
    connect(m_colorPanel, &ColorPanel::adjustChanged, this,
            [this](int saturation, int vibrance, int temperature, int tint) {
                m_curAdjust.saturation = saturation;
                m_curAdjust.vibrance = vibrance;
                m_curAdjust.temperature = temperature;
                m_curAdjust.tint = tint;
                emitAdjust();
            });
    connect(m_toneCurvePanel, &ToneCurvePanel::curveChanged, this,
            [this](const QVector<QPointF> &curve) {
                m_curAdjust.curve = curve;
                emitAdjust();
            });
    connect(m_levelsPanel, &LevelsPanel::levelsChanged, this,
            [this](const Levels &lv) {
                m_curAdjust.levels = lv;
                emitAdjust();
            });
    connect(m_detailEffectsPanel, &DetailEffectsPanel::adjustChanged, this,
            [this](int clarity, int sharpen, int vignette) {
                m_curAdjust.clarity = clarity;
                m_curAdjust.sharpen = sharpen;
                m_curAdjust.vignette = vignette;
                emitAdjust();
            });
    connect(m_maskPanel, &MaskPanel::maskTypeChanged, this, &LayersPanel::maskTypeChanged);
    connect(m_maskPanel, &MaskPanel::maskShapeChanged, this, &LayersPanel::maskShapeChanged);

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

void LayersPanel::setLevelsPreviewImage(const QImage &img) {
    if (m_active >= 0 && m_active < m_masks.size()) m_levelsPanel->setImage(img);
}

void LayersPanel::setMaskBrushRadius(double radiusNorm) {
    if (m_maskPanel) m_maskPanel->setBrushRadius(radiusNorm);
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
             m_name, m_opacity, m_blend, m_duplicate, m_delete, m_levelsPanel})
        w->setEnabled(has);
    if (has) {
        const Mask &m = m_masks[m_active];
        m_name->setText(m.name);
        m_opacity->setValue(int(std::lround(m.opacity * 100)));
        int blendIdx = m_blend->findData(int(m.blend));
        m_blend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);
        m_curAdjust = m.adj;
        m_tonePanel->setAdjustments(m_curAdjust.brightness, m_curAdjust.contrast,
                                     m_curAdjust.highlights, m_curAdjust.shadows);
        m_colorPanel->setAdjustments(m_curAdjust.saturation, m_curAdjust.vibrance,
                                      m_curAdjust.temperature, m_curAdjust.tint);
        m_toneCurvePanel->setCurve(m_curAdjust.curve);
        m_levelsPanel->setLevels(m_curAdjust.levels);
        m_detailEffectsPanel->setAdjustments(m_curAdjust.clarity, m_curAdjust.sharpen,
                                              m_curAdjust.vignette);
        m_maskPanel->setMask(m, true);
    } else {
        m_curAdjust = MaskAdjust();
        m_tonePanel->clear();
        m_colorPanel->clear();
        m_toneCurvePanel->clear();
        m_levelsPanel->clear();
        m_detailEffectsPanel->clear();
        m_maskPanel->clear();
    }
    m_syncing = false;
}

void LayersPanel::emitAdjust() {
    if (m_syncing) return;
    emit maskAdjustChanged(m_curAdjust);
}
