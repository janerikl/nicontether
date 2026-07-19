#include "ui/MaskPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

MaskPanel::MaskPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_hint = new QLabel("Select a layer in the Layers panel to edit its mask.");
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("color: #999;");
    root->addWidget(m_hint);

    auto *form = new QFormLayout;
    m_type = new QComboBox;
    m_type->addItem("None (whole image)", int(MaskType::None));
    m_type->addItem("Radial", int(MaskType::Radial));
    m_type->addItem("Graduated", int(MaskType::Linear));
    m_type->addItem("Brush", int(MaskType::Brush));
    form->addRow("Mask:", m_type);
    root->addLayout(form);

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
    root->addStretch(1);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_syncing) return;
                emit maskTypeChanged(MaskType(m_type->currentData().toInt()));
            });
    connect(m_invert, &QCheckBox::toggled, this, [this] { emitShape(); });
    connect(m_autoMask, &QCheckBox::toggled, this, [this] { emitShape(); });
    for (QSlider *s : {m_feather, m_hardness, m_brushSize})
        connect(s, &QSlider::valueChanged, this, [this] { emitShape(); });

    clear();
}

void MaskPanel::setBrushRadius(double radiusNorm) {
    if (!m_hasSelection) return;
    m_mask.brushRadius = radiusNorm;
    m_syncing = true;
    m_brushSize->setValue(int(std::lround(radiusNorm * 100)));
    m_syncing = false;
}

void MaskPanel::clear() {
    m_hasSelection = false;
    m_mask = Mask();
    setEnabled(false);
    loadMask();
}

void MaskPanel::setMask(const Mask &mask, bool hasSelection) {
    m_mask = mask;
    m_hasSelection = hasSelection;
    setEnabled(hasSelection);
    loadMask();
}

void MaskPanel::loadMask() {
    m_syncing = true;
    m_hint->setVisible(!m_hasSelection);
    int typeIdx = m_type->findData(int(m_mask.type));
    m_type->setCurrentIndex(typeIdx >= 0 ? typeIdx : 0);
    m_invert->setChecked(m_mask.inverted);
    m_feather->setValue(int(m_mask.feather * 100));
    m_hardness->setValue(int(m_mask.hardness * 100));
    m_brushSize->setValue(int(m_mask.brushRadius * 100));
    m_autoMask->setChecked(m_mask.autoMask);

    const bool brush = m_hasSelection && m_mask.type == MaskType::Brush;
    const bool geometric = m_hasSelection && m_mask.type != MaskType::None;
    m_hardnessLabel->setVisible(brush);
    m_hardness->setVisible(brush);
    m_brushSizeLabel->setVisible(brush);
    m_brushSize->setVisible(brush);
    m_autoMask->setVisible(brush);
    m_invert->setVisible(geometric);
    m_feather->setVisible(geometric);
    m_feather->setEnabled(geometric && m_mask.type != MaskType::Brush);
    m_syncing = false;
}

void MaskPanel::emitShape() {
    if (m_syncing) return;
    emit maskShapeChanged(m_invert->isChecked(), m_feather->value() / 100.0,
                          m_hardness->value() / 100.0,
                          m_brushSize->value() / 100.0,
                          m_autoMask->isChecked());
}
