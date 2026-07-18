#pragma once

#include <QWidget>
#include <QImage>

class QLabel;
class PannableScrollArea;
class QPushButton;
class QSlider;

// Embedded preview panel: shows a captured frame with continuous zoom (mouse
// wheel anchored to the cursor, or the zoom slider) and Space-drag panning.
class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow(QWidget *parent = nullptr);

    void showImage(const QString &path, const QImage &image);
    // Give the pannable view keyboard focus so Space-to-pan works immediately.
    void focusView();

private slots:
    void onWheelZoom(int steps, const QPoint &anchor);
    void onSliderChanged(int percent);

private:
    void applyScaling();
    double fitFactor() const;         // zoom factor that fits the viewport
    double currentFactor() const;     // effective factor in use right now
    void setZoomAnchored(double newZoom, const QPoint &anchorViewportPos);
    void syncSlider();

    PannableScrollArea *m_scroll = nullptr;
    QLabel *m_imageLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QPushButton *m_fitButton = nullptr;
    QPushButton *m_oneToOneButton = nullptr;
    QSlider *m_zoomSlider = nullptr;

    QImage m_image;
    double m_zoom = 1.0; // used when !m_fit
    bool m_fit = true;
};
