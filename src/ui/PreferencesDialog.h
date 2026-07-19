#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;

// File → Preferences… dialog. Holds the camera-model dropdown and the AF
// coordinate frame size used by click-to-focus. AF frame is remembered per
// model in QSettings; this dialog is the single writer.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

public slots:
    // Select a model by id (used for auto-detect). No-op if id is empty or
    // already the current selection, so a manual override survives reconnects.
    void selectModelById(const QString &id);

signals:
    void afFrameSizeChanged(int w, int h);

private:
    void onModelChanged();
    void onFrameEdited();
    void loadFrameForCurrentModel();
    QString currentModelId() const;

    QComboBox *m_model = nullptr;
    QSpinBox *m_frameW = nullptr;
    QSpinBox *m_frameH = nullptr;
};

// Returns the persisted AF frame for a model id (per-model override, else the
// model's built-in default, else 640x426). Shared by the dialog and startup
// seeding in RetouchWindow.
void afFrameForModel(const QString &id, int &w, int &h);
