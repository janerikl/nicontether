#pragma once

#include <QWidget>

class QSlider;

// Saturation/Vibrance/Temperature/Tint sliders for the selected layer's
// colour adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Color" label, so no header is drawn here (matches LevelsPanel/MaskPanel).
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget *parent = nullptr);

    void setAdjustments(int saturation, int vibrance, int temperature, int tint);
    void clear(); // no active layer

signals:
    void adjustChanged(int saturation, int vibrance, int temperature, int tint);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_saturation = nullptr;
    QSlider *m_vibrance = nullptr;
    QSlider *m_temperature = nullptr;
    QSlider *m_tint = nullptr;
};
