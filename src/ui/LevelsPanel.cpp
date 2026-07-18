#include "ui/LevelsPanel.h"

#include "ui/HistogramWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

LevelsPanel::LevelsPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Channel selector.
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Channel:"));
    m_channelCombo = new QComboBox;
    m_channelCombo->addItems({"RGB", "Red", "Green", "Blue"});
    top->addWidget(m_channelCombo, 1);
    root->addLayout(top);

    // Interactive histogram + handles.
    m_hist = new HistogramWidget;
    root->addWidget(m_hist, 1);

    // Numeric readouts: input black / gamma / white, output black / white.
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    m_inBlack = new QSpinBox;  m_inBlack->setRange(0, 254);
    m_gamma = new QDoubleSpinBox; m_gamma->setRange(0.10, 9.99);
    m_gamma->setSingleStep(0.01); m_gamma->setDecimals(2);
    m_inWhite = new QSpinBox;  m_inWhite->setRange(1, 255);
    m_outBlack = new QSpinBox; m_outBlack->setRange(0, 254);
    m_outWhite = new QSpinBox; m_outWhite->setRange(1, 255);

    grid->addWidget(new QLabel("Input"), 0, 0);
    grid->addWidget(m_inBlack, 0, 1);
    grid->addWidget(m_gamma, 0, 2);
    grid->addWidget(m_inWhite, 0, 3);
    grid->addWidget(new QLabel("Output"), 1, 0);
    grid->addWidget(m_outBlack, 1, 1);
    grid->addWidget(m_outWhite, 1, 3);
    root->addLayout(grid);

    // Auto / Reset.
    auto *btns = new QHBoxLayout;
    m_auto = new QPushButton("Auto");
    m_reset = new QPushButton("Reset");
    btns->addWidget(m_auto);
    btns->addWidget(m_reset);
    btns->addStretch(1);
    root->addLayout(btns);

    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onChannelComboChanged(); });
    connect(m_hist, &HistogramWidget::channelEdited, this,
            [this](const LevelsChannel &c) { writeActiveFromChannel(c); });
    for (QSpinBox *s : {m_inBlack, m_inWhite, m_outBlack, m_outWhite})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { onSpinChanged(); });
    connect(m_gamma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { onSpinChanged(); });
    connect(m_auto, &QPushButton::clicked, this, &LevelsPanel::onAuto);
    connect(m_reset, &QPushButton::clicked, this, &LevelsPanel::onReset);

    loadActiveIntoUi();
}

LevelsChannel &LevelsPanel::activeChannel() {
    switch (m_channelCombo->currentIndex()) {
    case 1: return m_levels.r;
    case 2: return m_levels.g;
    case 3: return m_levels.b;
    default: return m_levels.rgb;
    }
}

void LevelsPanel::setImage(const QImage &img) { m_hist->setImage(img); }

void LevelsPanel::clear() { m_hist->clear(); }

void LevelsPanel::setLevels(const Levels &levels) {
    m_levels = levels;
    loadActiveIntoUi();
}

void LevelsPanel::loadActiveIntoUi() {
    m_syncing = true;
    const int idx = m_channelCombo->currentIndex();
    m_hist->setDisplayChannel(static_cast<HistogramWidget::DisplayChannel>(idx));
    const LevelsChannel &c = activeChannel();
    m_hist->setLevelsChannel(c);
    m_inBlack->setValue(c.inBlack);
    m_gamma->setValue(c.gamma);
    m_inWhite->setValue(c.inWhite);
    m_outBlack->setValue(c.outBlack);
    m_outWhite->setValue(c.outWhite);
    m_syncing = false;
}

void LevelsPanel::writeActiveFromChannel(const LevelsChannel &c) {
    activeChannel() = c;
    // Refresh spinboxes without re-emitting.
    m_syncing = true;
    m_inBlack->setValue(c.inBlack);
    m_gamma->setValue(c.gamma);
    m_inWhite->setValue(c.inWhite);
    m_outBlack->setValue(c.outBlack);
    m_outWhite->setValue(c.outWhite);
    m_syncing = false;
    emit levelsChanged(m_levels);
}

void LevelsPanel::onChannelComboChanged() { loadActiveIntoUi(); }

void LevelsPanel::onSpinChanged() {
    if (m_syncing) return;
    LevelsChannel c = activeChannel();
    c.inBlack = std::min(m_inBlack->value(), m_inWhite->value() - 1);
    c.inWhite = std::max(m_inWhite->value(), m_inBlack->value() + 1);
    c.gamma = m_gamma->value();
    c.outBlack = std::min(m_outBlack->value(), m_outWhite->value() - 1);
    c.outWhite = std::max(m_outWhite->value(), m_outBlack->value() + 1);
    activeChannel() = c;
    m_hist->setLevelsChannel(c);
    emit levelsChanged(m_levels);
}

void LevelsPanel::onAuto() {
    QPair<int, int> range = m_hist->autoRange();
    LevelsChannel c = activeChannel();
    c.inBlack = std::min(range.first, 254);
    c.inWhite = std::max(range.second, c.inBlack + 1);
    activeChannel() = c;
    loadActiveIntoUi();
    emit levelsChanged(m_levels);
}

void LevelsPanel::onReset() {
    activeChannel() = LevelsChannel{};
    loadActiveIntoUi();
    emit levelsChanged(m_levels);
}
