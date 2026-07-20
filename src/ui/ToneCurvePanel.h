#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

class CurveEditor;

// Thin wrapper hosting the tone-curve editor for the selected layer, as its
// own dockable panel. The dock title provides the "Tone Curve" label.
class ToneCurvePanel : public QWidget {
    Q_OBJECT
public:
    explicit ToneCurvePanel(QWidget *parent = nullptr);

    void setCurve(const QVector<QPointF> &points); // no signal
    void clear(); // reset to identity + disable

signals:
    void curveChanged(const QVector<QPointF> &points);

private:
    CurveEditor *m_curve = nullptr;
};
