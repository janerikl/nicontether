#pragma once

#include <QWidget>

class QSlider;

// Brightness/Contrast/Highlights/Shadows sliders for the selected layer's
// tone adjustment. Purely a view — setAdjustments() loads values without
// emitting; user edits emit adjustChanged(). The dock title provides the
// "Tone" label, so no header is drawn here (matches LevelsPanel/MaskPanel).
class TonePanel : public QWidget {
    Q_OBJECT
public:
    explicit TonePanel(QWidget *parent = nullptr);

    void setAdjustments(int brightness, int contrast, int highlights, int shadows);
    void clear(); // no active layer

signals:
    void adjustChanged(int brightness, int contrast, int highlights, int shadows);

private:
    void emitChanged();

    bool m_syncing = false;
    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_highlights = nullptr;
    QSlider *m_shadows = nullptr;
};
