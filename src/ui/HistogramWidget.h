#pragma once

#include <QImage>
#include <QWidget>
#include <array>

#include "edit/Adjustments.h"

// Interactive Levels graph: draws the histogram for the selected channel and,
// beneath it, draggable Photoshop-style Levels handles — input black / gamma /
// white plus an output black / white gradient bar. Editing a handle emits
// channelEdited() with the updated LevelsChannel; the owning LevelsPanel routes
// it to the active image channel.
class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    enum DisplayChannel { Composite, Red, Green, Blue };

    explicit HistogramWidget(QWidget *parent = nullptr);

    void setImage(const QImage &img); // recompute bins from this preview image
    void clear();                     // no active photo: blank the panel

    void setDisplayChannel(DisplayChannel ch); // which channel to show + edit
    void setLevelsChannel(const LevelsChannel &c); // sync handles (no signal)
    LevelsChannel levelsChannel() const { return m_ch; }

    // Suggested input black/white for auto-levels on the displayed channel
    // (0.1% / 99.9% percentile clip). {0,255} if no data.
    QPair<int, int> autoRange() const;

    QSize sizeHint() const override { return QSize(256, 150); }
    QSize minimumSizeHint() const override { return QSize(160, 130); }

signals:
    void channelEdited(const LevelsChannel &c); // a handle was dragged

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    enum Handle { None, InBlack, Gamma, InWhite, OutBlack, OutWhite };

    void compute();
    QRectF graphRect() const;
    QRectF inputStripRect() const;
    QRectF outputBarRect() const;
    double xForValue(int v, const QRectF &area) const;
    int valueForX(double x, const QRectF &area) const;
    double gammaHandleValue() const; // input value where the gamma handle sits
    Handle hitTest(const QPointF &pos) const;

    QImage m_image; // current preview (for recompute on channel switch)
    DisplayChannel m_display = Composite;
    LevelsChannel m_ch;

    std::array<quint32, 256> m_r{};
    std::array<quint32, 256> m_g{};
    std::array<quint32, 256> m_b{};
    std::array<quint32, 256> m_luma{};
    quint32 m_maxBin = 0;
    bool m_hasData = false;
    bool m_clipLow = false;
    bool m_clipHigh = false;

    Handle m_drag = None;
};
