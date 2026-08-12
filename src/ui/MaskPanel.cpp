#include "ui/MaskPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QFontComboBox>
#include <QFont>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPushButton>
#include <QColorDialog>
#include "ui/ScrubSpinBox.h"
#include "ui/BrushPresetMenuButton.h"
#include <cmath>

MaskPanel::MaskPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_hint = new QLabel("Select a layer in the Layers panel to edit its mask.");
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("color: #999;");
    root->addWidget(m_hint);

    m_typeSection = new QWidget;
    auto *typeSectionLayout = new QVBoxLayout(m_typeSection);
    typeSectionLayout->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout;
    m_type = new QComboBox;
    m_type->addItem("None (whole image)", int(MaskType::None));
    m_type->addItem("Radial", int(MaskType::Radial));
    m_type->addItem("Graduated", int(MaskType::Linear));
    m_type->addItem("Brush", int(MaskType::Brush));
    m_type->addItem("Text (clip)", int(MaskType::Text));
    form->addRow("Mask:", m_type);
    typeSectionLayout->addLayout(form);
    root->addWidget(m_typeSection);

    auto *textForm = new QFormLayout;
    m_textContent = new QLineEdit;
    m_textContent->setPlaceholderText("Text…");
    textForm->addRow("Text:", m_textContent);
    auto *textStyleRow = new QHBoxLayout;
    m_textFont = new QFontComboBox;
    auto *textSizeScrub = new ScrubSpinBox;
    textSizeScrub->setScrubPixelsPerStep(6); // coarser: this field's range is only 1-40
    m_textSize = textSizeScrub;
    m_textSize->setRange(1, 40); // percent of image width, same scale as m_brushSize
    m_textSize->setSuffix("%");
    m_textBold = new QToolButton;
    m_textBold->setText("B");
    m_textBold->setCheckable(true);
    m_textItalic = new QToolButton;
    m_textItalic->setText("I");
    m_textItalic->setCheckable(true);
    textStyleRow->addWidget(m_textFont);
    textStyleRow->addWidget(m_textSize);
    textStyleRow->addWidget(m_textBold);
    textStyleRow->addWidget(m_textItalic);
    textForm->addRow("Style:", textStyleRow);
    root->addLayout(textForm);

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
    m_brushSizePx = new QLabel;
    m_brushSizePx->setMinimumWidth(40);
    m_brushSizePx->setStyleSheet("color: #999;");
    auto *brushSizeRow = new QHBoxLayout;
    brushSizeRow->addWidget(m_brushSize);
    brushSizeRow->addWidget(m_brushSizePx);
    shape->addRow(m_brushSizeLabel, brushSizeRow);
    m_brushPresets = new BrushPresetMenuButton;
    shape->addRow("Brush:", m_brushPresets);
    m_autoMask = new QCheckBox("Auto Mask (stop at edges)");
    shape->addRow(m_autoMask);
    m_gradientFill = new QCheckBox("Gradient Fill");
    shape->addRow(m_gradientFill);
    m_gradientColorABtn = new QPushButton;
    m_gradientColorABtn->setFixedWidth(50);
    m_gradientColorBBtn = new QPushButton;
    m_gradientColorBBtn->setFixedWidth(50);
    auto *gradientColorsRow = new QHBoxLayout;
    gradientColorsRow->addWidget(m_gradientColorABtn);
    gradientColorsRow->addWidget(m_gradientColorBBtn);
    shape->addRow("Colors:", gradientColorsRow);
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
    connect(m_gradientFill, &QCheckBox::toggled, this, [this] { emitGradientFill(); });
    connect(m_gradientColorABtn, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_gradientColorA, this, "Gradient Color A");
        if (c.isValid()) { m_gradientColorA = c; setColorButton(m_gradientColorABtn, c); emitGradientFill(); }
    });
    connect(m_gradientColorBBtn, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_gradientColorB, this, "Gradient Color B");
        if (c.isValid()) { m_gradientColorB = c; setColorButton(m_gradientColorBBtn, c); emitGradientFill(); }
    });
    connect(m_brushPresets, &BrushPresetMenuButton::presetApplied, this,
            [this](double brushRadius, double hardness) {
                m_brushSize->setValue(int(std::lround(brushRadius * 100)));
                m_hardness->setValue(int(std::lround(hardness * 100)));
            });
    connect(m_brushPresets, &QToolButton::pressed, this, [this] {
        m_brushPresets->setCurrentValues(m_brushSize->value() / 100.0,
                                         m_hardness->value() / 100.0);
    });

    connect(m_textContent, &QLineEdit::textChanged, this, [this] { emitText(); });
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this] { emitText(); });
    connect(m_textSize, &QSpinBox::valueChanged, this, [this] { emitText(); });
    connect(m_textBold, &QToolButton::toggled, this, [this] { emitText(); });
    connect(m_textItalic, &QToolButton::toggled, this, [this] { emitText(); });

    clear();
}

void MaskPanel::setBrushRadius(double radiusNorm) {
    if (!m_hasSelection) return;
    m_mask.brushRadius = radiusNorm;
    m_syncing = true;
    m_brushSize->setValue(int(std::lround(radiusNorm * 100)));
    m_syncing = false;
    updateBrushSizePxLabel();
}

void MaskPanel::setImageWidth(int width) {
    m_imageWidth = width;
    updateBrushSizePxLabel();
}

void MaskPanel::updateBrushSizePxLabel() {
    if (m_imageWidth <= 0) {
        m_brushSizePx->clear();
        return;
    }
    const int px = int(std::lround(m_brushSize->value() / 100.0 * m_imageWidth));
    m_brushSizePx->setText(QString("%1px").arg(px));
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
    // Shape and TextBox layers have no maskable "kind" of their own — their
    // `type` field is the layer's own identity (what to draw), not a mask
    // applied on top of something else, so the type combo (and every
    // mask-shape control below) doesn't apply to them at all.
    const bool maskable = m_hasSelection && m_mask.type != MaskType::Shape &&
                          m_mask.type != MaskType::TextBox;
    m_hint->setVisible(!m_hasSelection ||
                       (m_hasSelection && !maskable));
    m_hint->setText(m_hasSelection && !maskable
                        ? "This layer doesn't use a mask."
                        : "Select a layer in the Layers panel to edit its mask.");
    m_typeSection->setVisible(maskable);
    if (!maskable) {
        for (QWidget *w : std::initializer_list<QWidget *>{
                 m_invert, m_feather, m_hardnessLabel, m_hardness, m_brushSizeLabel,
                 m_brushSize, m_brushSizePx, m_brushPresets, m_autoMask, m_textContent,
                 m_textFont, m_textSize, m_textBold, m_textItalic, m_gradientFill,
                 m_gradientColorABtn, m_gradientColorBBtn})
            w->setVisible(false);
        m_syncing = false;
        return;
    }
    int typeIdx = m_type->findData(int(m_mask.type));
    m_type->setCurrentIndex(typeIdx >= 0 ? typeIdx : 0);
    m_invert->setChecked(m_mask.inverted);
    m_feather->setValue(int(m_mask.feather * 100));
    m_hardness->setValue(int(m_mask.hardness * 100));
    m_brushSize->setValue(int(m_mask.brushRadius * 100));
    updateBrushSizePxLabel();
    m_autoMask->setChecked(m_mask.autoMask);
    m_gradientFill->setChecked(m_mask.isGradientFill);
    m_gradientColorA = m_mask.gradientColorA;
    m_gradientColorB = m_mask.gradientColorB;
    setColorButton(m_gradientColorABtn, m_gradientColorA);
    setColorButton(m_gradientColorBBtn, m_gradientColorB);
    m_textContent->setText(m_mask.text);
    m_textFont->setCurrentFont(QFont(m_mask.textFamily));
    m_textSize->setValue(int(std::lround(m_mask.textPixelSize * 100)));
    m_textBold->setChecked(m_mask.textBold);
    m_textItalic->setChecked(m_mask.textItalic);

    const bool brush = m_hasSelection && m_mask.type == MaskType::Brush;
    const bool text = m_hasSelection && m_mask.type == MaskType::Text;
    const bool geometric = m_hasSelection && m_mask.type != MaskType::None &&
                           m_mask.type != MaskType::Text;
    m_hardnessLabel->setVisible(brush);
    m_hardness->setVisible(brush);
    m_brushSizeLabel->setVisible(brush);
    m_brushSize->setVisible(brush);
    m_brushSizePx->setVisible(brush);
    m_brushPresets->setVisible(brush);
    m_autoMask->setVisible(brush);
    m_invert->setVisible(geometric);
    m_feather->setVisible(geometric);
    m_feather->setEnabled(geometric && m_mask.type != MaskType::Brush);
    const bool gradientCapable = m_hasSelection &&
                                 (m_mask.type == MaskType::Radial || m_mask.type == MaskType::Linear);
    m_gradientFill->setVisible(gradientCapable);
    m_gradientColorABtn->setVisible(gradientCapable);
    m_gradientColorBBtn->setVisible(gradientCapable);
    m_textContent->setVisible(text);
    m_textFont->setVisible(text);
    m_textSize->setVisible(text);
    m_textBold->setVisible(text);
    m_textItalic->setVisible(text);
    m_syncing = false;
}

void MaskPanel::emitText() {
    if (m_syncing) return;
    emit maskTextChanged(m_textContent->text(), m_textFont->currentFont().family(),
                         m_textSize->value() / 100.0, m_textBold->isChecked(),
                         m_textItalic->isChecked());
}

void MaskPanel::emitShape() {
    updateBrushSizePxLabel();
    if (m_syncing) return;
    emit maskShapeChanged(m_invert->isChecked(), m_feather->value() / 100.0,
                          m_hardness->value() / 100.0,
                          m_brushSize->value() / 100.0,
                          m_autoMask->isChecked());
}

void MaskPanel::emitGradientFill() {
    if (m_syncing) return;
    emit gradientFillChanged(m_gradientFill->isChecked(), m_gradientColorA, m_gradientColorB);
}

void MaskPanel::setColorButton(QPushButton *btn, const QColor &color) {
    btn->setStyleSheet(QString("background-color: %1;").arg(color.name()));
}
