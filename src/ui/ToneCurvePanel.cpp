#include "ui/ToneCurvePanel.h"

#include "edit/CurveEditor.h"

#include <QVBoxLayout>

ToneCurvePanel::ToneCurvePanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    m_curve = new CurveEditor;
    root->addWidget(m_curve);
    connect(m_curve, &CurveEditor::curveChanged, this, &ToneCurvePanel::curveChanged);
    setEnabled(false);
}

void ToneCurvePanel::setCurve(const QVector<QPointF> &points) {
    setEnabled(true);
    m_curve->setCurve(points);
}

void ToneCurvePanel::clear() {
    m_curve->resetCurve();
    setEnabled(false);
}
