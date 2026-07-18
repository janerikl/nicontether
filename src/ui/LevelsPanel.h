#pragma once

#include <QImage>
#include <QWidget>

#include "edit/Adjustments.h"

class HistogramWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;

// Photoshop-style Levels panel: a channel selector (RGB / Red / Green / Blue),
// the interactive histogram graph with input/output handles, numeric readouts,
// and Auto / Reset buttons. Owns the full Levels struct and emits levelsChanged
// whenever the user edits it. Fed the tab's current preview image for display.
class LevelsPanel : public QWidget {
    Q_OBJECT
public:
    explicit LevelsPanel(QWidget *parent = nullptr);

    void setImage(const QImage &img);      // preview to display in the histogram
    void setLevels(const Levels &levels);  // sync UI from a tab (no signal)
    void clear();                          // no active photo

signals:
    void levelsChanged(const Levels &levels);

private:
    LevelsChannel &activeChannel();              // the channel the combo selects
    void loadActiveIntoUi();                     // combo/handles/spinboxes <- active
    void writeActiveFromChannel(const LevelsChannel &c); // active <- edit, then emit
    void onChannelComboChanged();
    void onSpinChanged();
    void onAuto();
    void onReset();

    Levels m_levels;
    bool m_syncing = false;

    HistogramWidget *m_hist = nullptr;
    QComboBox *m_channelCombo = nullptr;
    QSpinBox *m_inBlack = nullptr;
    QDoubleSpinBox *m_gamma = nullptr;
    QSpinBox *m_inWhite = nullptr;
    QSpinBox *m_outBlack = nullptr;
    QSpinBox *m_outWhite = nullptr;
    QPushButton *m_auto = nullptr;
    QPushButton *m_reset = nullptr;
};
