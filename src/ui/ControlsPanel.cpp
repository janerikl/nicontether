#include "ui/ControlsPanel.h"

#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSettings>

ControlsPanel::ControlsPanel(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);

    auto *heading = new QLabel("<b>Camera Controls</b>");
    outer->addWidget(heading);

    m_form = new QFormLayout;
    outer->addLayout(m_form);

    outer->addStretch(1);

    // Click-to-focus calibration: AF coordinate frame size. Nikon's changeafarea
    // wants coordinates in the live-view header's ImageWidth/Height frame, which
    // libgphoto2 discards — so it is user-adjustable. Center is always correct;
    // tune these until edge clicks focus where the reticle is drawn.
    auto *afForm = new QFormLayout;
    m_afFrameW = new QSpinBox;
    m_afFrameH = new QSpinBox;
    m_afFrameW->setRange(1, 20000);
    m_afFrameH->setRange(1, 20000);
    afForm->addRow("AF frame width:", m_afFrameW);
    afForm->addRow("AF frame height:", m_afFrameH);
    outer->addLayout(afForm);

    loadAfFrameSettings();

    auto persist = [this]() {
        QSettings s;
        s.setValue("af/frameWidth", m_afFrameW->value());
        s.setValue("af/frameHeight", m_afFrameH->value());
        emit afFrameSizeChanged(m_afFrameW->value(), m_afFrameH->value());
    };
    connect(m_afFrameW, qOverload<int>(&QSpinBox::valueChanged), this,
            [persist](int) { persist(); });
    connect(m_afFrameH, qOverload<int>(&QSpinBox::valueChanged), this,
            [persist](int) { persist(); });

    m_afButton = new QPushButton("Autofocus");
    m_captureButton = new QPushButton("Capture");
    m_captureButton->setMinimumHeight(48);
    QFont f = m_captureButton->font();
    f.setBold(true);
    m_captureButton->setFont(f);

    outer->addWidget(m_afButton);
    outer->addWidget(m_captureButton);

    connect(m_afButton, &QPushButton::clicked, this, &ControlsPanel::autofocusRequested);
    connect(m_captureButton, &QPushButton::clicked, this, &ControlsPanel::captureRequested);

    setEnabledControls(false);

    emit afFrameSizeChanged(m_afFrameW->value(), m_afFrameH->value());
}

void ControlsPanel::loadAfFrameSettings() {
    QSettings s;
    // Default 640x426: a starting point for the D750 / D7x00 / D5x00 family.
    // The user calibrates from here.
    int w = s.value("af/frameWidth", 640).toInt();
    int h = s.value("af/frameHeight", 426).toInt();
    QSignalBlocker bw(m_afFrameW);
    QSignalBlocker bh(m_afFrameH);
    m_afFrameW->setValue(w);
    m_afFrameH->setValue(h);
}

QComboBox *ControlsPanel::addRow(const QString &label) {
    auto *combo = new QComboBox;
    m_form->addRow(label + ":", combo);
    return combo;
}

void ControlsPanel::populate(const ConfigOptionMap &options) {
    // Clear existing rows.
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_rows.clear();

    // Preferred display order.
    const QStringList order = {"shutterspeed", "aperture", "iso",
                               "whitebalance", "imagequality"};
    for (const QString &key : order) {
        if (!options.contains(key)) continue;
        const ConfigOption &opt = options.value(key);

        QComboBox *combo = addRow(opt.label);
        combo->addItems(opt.choices);
        combo->setCurrentText(opt.current);
        combo->setEnabled(!opt.readOnly);

        ControlRow row;
        row.widgetName = opt.widgetName;
        row.combo = combo;
        m_rows.insert(key, row);

        const QString widgetName = opt.widgetName;
        connect(combo, &QComboBox::currentTextChanged, this,
                [this, widgetName](const QString &text) {
                    emit configEditRequested(widgetName, text);
                });
    }
    setEnabledControls(true);
}

void ControlsPanel::setEnabledControls(bool enabled) {
    for (auto &row : m_rows)
        if (row.combo) row.combo->setEnabled(enabled);
    m_afButton->setEnabled(enabled);
    m_captureButton->setEnabled(enabled);
}

void ControlsPanel::updateValue(const QString &widgetName, const QString &value) {
    for (auto &row : m_rows) {
        if (row.widgetName == widgetName && row.combo) {
            QSignalBlocker block(row.combo);
            row.combo->setCurrentText(value);
            return;
        }
    }
}
