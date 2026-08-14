#include "ui/LayerAdjustmentsPanel.h"

#include "ui/ColorPanel.h"
#include "ui/DetailEffectsPanel.h"
#include "ui/LevelsPanel.h"
#include "ui/MaskPanel.h"
#include "ui/ToneCurvePanel.h"
#include "ui/TonePanel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

LayerAdjustmentsPanel::LayerAdjustmentsPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_header = new QLabel;
    QFont f = m_header->font();
    f.setBold(true);
    m_header->setFont(f);
    root->addWidget(m_header);

    m_stack = new QStackedWidget;
    root->addWidget(m_stack, 1);

    m_tonePanel = new TonePanel;
    m_stack->insertWidget(Tone, m_tonePanel);

    m_colorPanel = new ColorPanel;
    m_stack->insertWidget(Colour, m_colorPanel);

    m_toneCurvePanel = new ToneCurvePanel;
    m_stack->insertWidget(ToneCurveSection, m_toneCurvePanel);

    m_levelsPanel = new LevelsPanel;
    // The targeted color-adjustment tool only edits the global (base) levels;
    // hide its toggle in the per-layer panel.
    m_levelsPanel->setTargetPickVisible(false);
    m_stack->insertWidget(Levels, m_levelsPanel);

    m_detailEffectsPanel = new DetailEffectsPanel;
    m_stack->insertWidget(DetailEffects, m_detailEffectsPanel);

    m_maskPanel = new MaskPanel;
    m_stack->insertWidget(Masks, m_maskPanel);

    // Remove Object section: a flat checkable list (no grouping/reordering
    // needed), one row per cached content-aware fill, plus a Delete button —
    // mirrors the masks list's eye-toggle + Delete pattern.
    auto *removalsContent = new QWidget;
    auto *removalsLayout = new QVBoxLayout(removalsContent);
    removalsLayout->setContentsMargins(0, 0, 0, 0);
    m_removalList = new QListWidget;
    m_removalList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_removalList->setMinimumHeight(100);
    removalsLayout->addWidget(m_removalList, 1);
    auto *removalButtons = new QHBoxLayout;
    m_deleteRemoval = new QPushButton("Delete");
    removalButtons->addWidget(m_deleteRemoval);
    removalButtons->addStretch(1);
    removalsLayout->addLayout(removalButtons);
    m_stack->insertWidget(RemoveObject, removalsContent);

    showSection(Tone);
}

QString LayerAdjustmentsPanel::sectionTitle(Section section) {
    switch (section) {
    case Tone:             return "Tone";
    case Colour:            return "Colour";
    case ToneCurveSection:  return "Tone Curve";
    case Levels:            return "Levels";
    case DetailEffects:     return "Detail & Effects";
    case Masks:             return "Masks";
    case RemoveObject:      return "Remove Object";
    default:                return QString();
    }
}

void LayerAdjustmentsPanel::showSection(Section section) {
    if (section < 0 || section >= SectionCount) return;
    m_stack->setCurrentIndex(section);
    m_header->setText(sectionTitle(section));
}
