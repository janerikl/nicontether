#include "ui/TonePanel.h"

#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>

TonePanel::TonePanel(QWidget *parent) : QWidget(parent) {
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
    m_brightness = mk("Brightness");
    m_contrast = mk("Contrast");
    m_highlights = mk("Highlights");
    m_shadows = mk("Shadows");
    root->addLayout(form);
    root->addStretch(1);

    clear();
}

void TonePanel::setAdjustments(int brightness, int contrast, int highlights, int shadows) {
    m_syncing = true;
    setEnabled(true);
    m_brightness->setValue(brightness);
    m_contrast->setValue(contrast);
    m_highlights->setValue(highlights);
    m_shadows->setValue(shadows);
    m_syncing = false;
}

void TonePanel::clear() {
    m_syncing = true;
    m_brightness->setValue(0);
    m_contrast->setValue(0);
    m_highlights->setValue(0);
    m_shadows->setValue(0);
    m_syncing = false;
    setEnabled(false);
}

void TonePanel::emitChanged() {
    if (m_syncing) return;
    emit adjustChanged(m_brightness->value(), m_contrast->value(),
                        m_highlights->value(), m_shadows->value());
}
