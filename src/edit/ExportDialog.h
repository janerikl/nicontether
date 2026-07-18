#pragma once

#include <QDialog>

#include "edit/ExportPreset.h"

class QComboBox;
class QSpinBox;
class QPushButton;

// Lets the user pick an export preset, tweak its fields, and optionally save the
// tweaks as a new/updated custom preset. On accept, selectedPreset() returns the
// effective settings to export with.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(ExportPresetStore *store, QWidget *parent = nullptr);

    ExportPreset selectedPreset() const;

private slots:
    void loadPresetIntoFields(int index);
    void onFormatChanged();
    void onSavePreset();
    void onDeletePreset();

private:
    void refreshPresetList(const QString &selectName = QString());

    ExportPresetStore *m_store;
    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QSpinBox *m_longEdge = nullptr;
    QSpinBox *m_quality = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
};
