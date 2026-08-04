#include "ui/DetailEffectsPanel.h"

#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

DetailEffectsPanel::DetailEffectsPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *form = new QFormLayout;
    m_clarity = new QSlider(Qt::Horizontal);
    m_clarity->setRange(-100, 100);
    form->addRow("Clarity:", m_clarity);
    m_sharpen = new QSlider(Qt::Horizontal);
    m_sharpen->setRange(0, 100);
    form->addRow("Sharpen:", m_sharpen);
    m_vignette = new QSlider(Qt::Horizontal);
    m_vignette->setRange(-100, 100);
    form->addRow("Vignette:", m_vignette);
    form->addRow(new QLabel("<i>Lighting</i>"));
    m_lightAngle = new QSlider(Qt::Horizontal);
    m_lightAngle->setRange(0, 360);
    form->addRow("Light Angle:", m_lightAngle);
    m_lightIntensity = new QSlider(Qt::Horizontal);
    m_lightIntensity->setRange(-100, 100);
    form->addRow("Light Intensity:", m_lightIntensity);
    root->addLayout(form);
    root->addStretch(1);

    connect(m_clarity, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_sharpen, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_vignette, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_lightAngle, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(m_lightIntensity, &QSlider::valueChanged, this, [this] { emitChanged(); });

    clear();
}

void DetailEffectsPanel::setAdjustments(int clarity, int sharpen, int vignette,
                                        int lightAngle, int lightIntensity) {
    m_syncing = true;
    setEnabled(true);
    m_clarity->setValue(clarity);
    m_sharpen->setValue(sharpen);
    m_vignette->setValue(vignette);
    m_lightAngle->setValue(lightAngle);
    m_lightIntensity->setValue(lightIntensity);
    m_syncing = false;
}

void DetailEffectsPanel::clear() {
    m_syncing = true;
    m_clarity->setValue(0);
    m_sharpen->setValue(0);
    m_vignette->setValue(0);
    m_lightAngle->setValue(0);
    m_lightIntensity->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void DetailEffectsPanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_clarity->value(), m_sharpen->value(), m_vignette->value(),
                       m_lightAngle->value(), m_lightIntensity->value());
}
