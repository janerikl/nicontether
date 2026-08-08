#include "ui/BrushPresetMenuButton.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStringList>

BrushPresetMenuButton::BrushPresetMenuButton(QWidget *parent) : QToolButton(parent) {
    setText("Presets");
    setPopupMode(QToolButton::InstantPopup);
    m_menu = new QMenu(this);
    setMenu(m_menu);
    connect(m_menu, &QMenu::aboutToShow, this, &BrushPresetMenuButton::rebuildMenu);
}

void BrushPresetMenuButton::setCurrentValues(double brushRadius, double hardness) {
    m_currentRadius = brushRadius;
    m_currentHardness = hardness;
}

void BrushPresetMenuButton::rebuildMenu() {
    m_menu->clear();
    m_store = BrushPresetStore(); // reload from QSettings so both call sites stay in sync

    const QList<BrushPreset> builtins = BrushPresetStore::builtins();
    for (const BrushPreset &preset : builtins) {
        auto *act = m_menu->addAction(preset.name);
        connect(act, &QAction::triggered, this, [this, preset] {
            emit presetApplied(preset.brushRadius, preset.hardness);
        });
    }

    const QList<BrushPreset> &custom = m_store.custom();
    if (!custom.isEmpty()) {
        m_menu->addSeparator();
        for (const BrushPreset &preset : custom) {
            auto *act = m_menu->addAction(preset.name);
            connect(act, &QAction::triggered, this, [this, preset] {
                emit presetApplied(preset.brushRadius, preset.hardness);
            });
        }
    }

    m_menu->addSeparator();
    auto *saveAct = m_menu->addAction("Save Current as Preset…");
    connect(saveAct, &QAction::triggered, this, &BrushPresetMenuButton::onSavePreset);
    if (!custom.isEmpty()) {
        auto *deleteAct = m_menu->addAction("Delete Preset…");
        connect(deleteAct, &QAction::triggered, this, &BrushPresetMenuButton::onDeletePreset);
    }
}

void BrushPresetMenuButton::onSavePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Brush Preset", "Preset name:",
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    BrushPreset preset;
    preset.name = name.trimmed();
    preset.brushRadius = m_currentRadius;
    preset.hardness = m_currentHardness;
    m_store.addOrUpdate(preset);
}

void BrushPresetMenuButton::onDeletePreset() {
    const QList<BrushPreset> &custom = m_store.custom();
    if (custom.isEmpty()) return;
    QStringList names;
    for (const BrushPreset &p : custom) names << p.name;
    bool ok = false;
    const QString name = QInputDialog::getItem(this, "Delete Brush Preset", "Preset:", names, 0,
                                                false, &ok);
    if (!ok || name.isEmpty()) return;
    m_store.remove(name);
}
