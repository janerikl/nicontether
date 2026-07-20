#include "ui/ColorPanel.h"

#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>

ColorPanel::ColorPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *form = new QFormLayout;
    auto mk = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        form->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitChanged(); });
        return s;
    };
    m_saturation = mk("Saturation");
    m_vibrance = mk("Vibrance");
    m_temperature = mk("Temperature");
    m_tint = mk("Tint (green/magenta)");
    root->addLayout(form);
    root->addStretch(1);

    clear();
}

void ColorPanel::setAdjustments(int saturation, int vibrance, int temperature, int tint) {
    m_syncing = true;
    setEnabled(true);
    m_saturation->setValue(saturation);
    m_vibrance->setValue(vibrance);
    m_temperature->setValue(temperature);
    m_tint->setValue(tint);
    m_syncing = false;
}

void ColorPanel::clear() {
    m_syncing = true;
    m_saturation->setValue(0);
    m_vibrance->setValue(0);
    m_temperature->setValue(0);
    m_tint->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void ColorPanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_saturation->value(), m_vibrance->value(),
                        m_temperature->value(), m_tint->value());
}
