#pragma once
#include <QWidget>
#include <QColor>

// Photoshop-style foreground/background color swatch: two overlapping
// squares (fg front, bg back), a swap arrow, and a reset-to-default icon.
// Click a square to open QColorDialog and change that color.
class ColorSwatchWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatchWidget(QWidget *parent = nullptr);

    QColor foregroundColor() const { return m_fg; }
    QColor backgroundColor() const { return m_bg; }

    QSize sizeHint() const override { return QSize(36, 36); }

public slots:
    void swapColors();
    void resetColors();
    void setForegroundColor(const QColor &color);

signals:
    void foregroundColorChanged(const QColor &color);
    void backgroundColorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect fgRect() const;
    QRect bgRect() const;
    QRect swapRect() const;
    QRect resetRect() const;

    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;
};
