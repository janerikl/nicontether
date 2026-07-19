#include "ui/MaskPanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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

MaskPanel::MaskPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Layers are added from the tool's flyout in the sidebar (Photoshop-style),
    // so this panel is edit-only: pick the active layer, reorder, set its
    // shape/opacity/blend, and adjust its tone/colour.

    // Layer list: checkable (visibility) rows, drag to reorder, delete button.
    auto *selRow = new QHBoxLayout;
    m_maskList = new QListWidget;
    m_maskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_maskList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_delete = new QPushButton("Delete");
    selRow->addWidget(m_maskList, 1);
    selRow->addWidget(m_delete);
    root->addLayout(selRow);

    m_hint = new QLabel("Drag on the image to draw the mask.");
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("color: #999;");
    root->addWidget(m_hint);

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
    root->addLayout(props);

    // Shape controls.
    auto *shape = new QFormLayout;
    m_invert = new QCheckBox("Invert mask");
    shape->addRow(m_invert);
    m_feather = new QSlider(Qt::Horizontal);
    m_feather->setRange(0, 100);
    shape->addRow("Feather:", m_feather);
    m_hardness = new QSlider(Qt::Horizontal);
    m_hardness->setRange(0, 100);
    m_hardnessLabel = new QLabel("Hardness:");
    shape->addRow(m_hardnessLabel, m_hardness);
    m_brushSize = new QSlider(Qt::Horizontal);
    m_brushSize->setRange(1, 40); // percent of image width
    m_brushSizeLabel = new QLabel("Brush size:");
    shape->addRow(m_brushSizeLabel, m_brushSize);
    m_autoMask = new QCheckBox("Auto Mask (stop at edges)");
    shape->addRow(m_autoMask);
    root->addLayout(shape);

    // Tone/colour sliders.
    root->addWidget(new QLabel("<b>Local adjustments</b>"));
    auto *form = new QFormLayout;
    auto mk = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitAdjust(); });
        return s;
    };
    m_brightness = mk("Brightness");
    m_contrast = mk("Contrast");
    m_highlights = mk("Highlights");
    m_shadows = mk("Shadows");
    m_saturation = mk("Saturation");
    m_vibrance = mk("Vibrance");
    m_temperature = mk("Temperature");
    m_tint = mk("Tint");
    root->addLayout(form);
    root->addStretch(1);

    connect(m_delete, &QPushButton::clicked, this,
            [this] { emit deleteMaskRequested(); });
    connect(m_maskList, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!m_syncing) emit selectMaskRequested(i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                if (i == m_active)
                    emit maskVisibleChanged(item->checkState() == Qt::Checked);
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
    connect(m_invert, &QCheckBox::toggled, this, [this] { emitShape(); });
    connect(m_autoMask, &QCheckBox::toggled, this, [this] { emitShape(); });
    for (QSlider *s : {m_feather, m_hardness, m_brushSize})
        connect(s, &QSlider::valueChanged, this, [this] { emitShape(); });

    clear();
}

void MaskPanel::setBrushRadius(double radiusNorm) {
    if (m_active < 0 || m_active >= m_masks.size()) return;
    m_masks[m_active].brushRadius = radiusNorm;
    m_syncing = true;
    m_brushSize->setValue(int(std::lround(radiusNorm * 100)));
    m_syncing = false;
}

void MaskPanel::clear() {
    m_masks.clear();
    m_active = -1;
    m_syncing = true;
    m_maskList->clear();
    m_syncing = false;
    setEnabled(false);
}

void MaskPanel::setMasks(const QVector<Mask> &masks, int activeIndex) {
    m_masks = masks;
    m_active = activeIndex;
    setEnabled(true);
    rebuildList();
    loadActive();
}

void MaskPanel::rebuildList() {
    m_syncing = true;
    m_maskList->clear();
    for (int i = 0; i < m_masks.size(); ++i) {
        const Mask &m = m_masks[i];
        QString label = m.name.isEmpty()
                            ? QString("Layer %1 (%2)").arg(i + 1).arg(maskTypeLabel(m.type))
                            : m.name;
        auto *item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
        m_maskList->addItem(item);
    }
    if (m_active >= 0 && m_active < m_masks.size())
        m_maskList->setCurrentRow(m_active);
    m_syncing = false;
}

void MaskPanel::loadActive() {
    const bool has = m_active >= 0 && m_active < m_masks.size();
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_invert, m_feather, m_hardness,
             m_brushSize, m_autoMask, m_delete, m_brightness, m_contrast,
             m_highlights, m_shadows, m_saturation, m_vibrance, m_temperature,
             m_tint})
        w->setEnabled(has);
    if (has) {
        const Mask &m = m_masks[m_active];
        m_name->setText(m.name);
        m_opacity->setValue(int(std::lround(m.opacity * 100)));
        int blendIdx = m_blend->findData(int(m.blend));
        m_blend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);
        m_invert->setChecked(m.inverted);
        m_feather->setValue(int(m.feather * 100));
        m_hardness->setValue(int(m.hardness * 100));
        m_brushSize->setValue(int(m.brushRadius * 100));
        m_autoMask->setChecked(m.autoMask);
        const bool brush = m.type == MaskType::Brush;
        const bool geometric = m.type != MaskType::None;
        m_hardnessLabel->setVisible(brush);
        m_hardness->setVisible(brush);
        m_brushSizeLabel->setVisible(brush);
        m_brushSize->setVisible(brush);
        m_autoMask->setVisible(brush);
        m_invert->setVisible(geometric);
        m_feather->setVisible(geometric);
        m_feather->setEnabled(geometric && m.type != MaskType::Brush);
        const MaskAdjust &a = m.adj;
        m_brightness->setValue(a.brightness);
        m_contrast->setValue(a.contrast);
        m_highlights->setValue(a.highlights);
        m_shadows->setValue(a.shadows);
        m_saturation->setValue(a.saturation);
        m_vibrance->setValue(a.vibrance);
        m_temperature->setValue(a.temperature);
        m_tint->setValue(a.tint);
    }
    m_syncing = false;
}

void MaskPanel::emitAdjust() {
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
    emit maskAdjustChanged(a);
}

void MaskPanel::emitShape() {
    if (m_syncing) return;
    emit maskShapeChanged(m_invert->isChecked(), m_feather->value() / 100.0,
                          m_hardness->value() / 100.0,
                          m_brushSize->value() / 100.0,
                          m_autoMask->isChecked());
}
