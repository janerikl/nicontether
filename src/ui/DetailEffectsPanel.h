#pragma once

#include <QWidget>

class QSlider;

// Clarity/Sharpen/Vignette sliders for the selected layer's detail/effects
// adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Detail & Effects" label, so no header is drawn here (matches
// LevelsPanel/MaskPanel).
class DetailEffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailEffectsPanel(QWidget *parent = nullptr);

    void setAdjustments(int clarity, int sharpen, int vignette);
    void clear(); // no active layer

signals:
    void adjustChanged(int clarity, int sharpen, int vignette);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_clarity = nullptr;
    QSlider *m_sharpen = nullptr;
    QSlider *m_vignette = nullptr;
};
