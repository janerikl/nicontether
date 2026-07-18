#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>

// A small tone-curve editor. Control points are in [0,1]×[0,1] (x = input,
// y = output). Drag points to reshape; click empty space to add a point;
// right-click a point to remove it. Emits curveChanged with the sorted points.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);

    void setCurve(const QVector<QPointF> &points); // no signal
    QVector<QPointF> curve() const { return m_points; }
    void resetCurve();

signals:
    void curveChanged(const QVector<QPointF> &points);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QPointF toWidget(const QPointF &p) const;  // [0,1] -> pixels
    QPointF toCurve(const QPointF &p) const;   // pixels -> [0,1]
    int nearestPoint(const QPointF &widgetPos, double maxDist) const;

    QVector<QPointF> m_points; // sorted by x
    int m_dragIndex = -1;
};
