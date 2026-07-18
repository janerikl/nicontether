#include "edit/CurveEditor.h"

#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

namespace {
constexpr int kMargin = 6;
constexpr double kPickDist = 12.0;
}

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent) {
    setMinimumSize(180, 140);
    resetCurve();
}

void CurveEditor::resetCurve() {
    m_points = {QPointF(0, 0), QPointF(1, 1)};
    update();
}

void CurveEditor::setCurve(const QVector<QPointF> &points) {
    if (points.size() >= 2) {
        m_points = points;
        std::sort(m_points.begin(), m_points.end(),
                  [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    } else {
        m_points = {QPointF(0, 0), QPointF(1, 1)};
    }
    update();
}

QPointF CurveEditor::toWidget(const QPointF &p) const {
    double w = width() - 2 * kMargin;
    double h = height() - 2 * kMargin;
    return QPointF(kMargin + p.x() * w, kMargin + (1.0 - p.y()) * h);
}

QPointF CurveEditor::toCurve(const QPointF &p) const {
    double w = width() - 2 * kMargin;
    double h = height() - 2 * kMargin;
    double x = (p.x() - kMargin) / std::max(1.0, w);
    double y = 1.0 - (p.y() - kMargin) / std::max(1.0, h);
    return QPointF(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0));
}

int CurveEditor::nearestPoint(const QPointF &pos, double maxDist) const {
    int best = -1;
    double bestD = maxDist;
    for (int i = 0; i < m_points.size(); ++i) {
        double d = QLineF(pos, toWidget(m_points[i])).length();
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void CurveEditor::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(40, 40, 40));

    // Grid.
    p.setPen(QColor(70, 70, 70));
    for (int i = 1; i < 4; ++i) {
        double fx = i / 4.0;
        p.drawLine(toWidget(QPointF(fx, 0)), toWidget(QPointF(fx, 1)));
        p.drawLine(toWidget(QPointF(0, fx)), toWidget(QPointF(1, fx)));
    }

    // Curve polyline (sampled).
    p.setPen(QPen(QColor(230, 230, 230), 2));
    QPointF prev;
    for (int i = 0; i <= 64; ++i) {
        double x = i / 64.0;
        // linear interpolation between control points
        double y = m_points.first().y();
        if (x >= m_points.last().x()) y = m_points.last().y();
        else if (x > m_points.first().x()) {
            int k = 1;
            while (k < m_points.size() && m_points[k].x() < x) ++k;
            const QPointF &a = m_points[k - 1];
            const QPointF &b = m_points[k];
            double t = (x - a.x()) / std::max(1e-6, b.x() - a.x());
            y = a.y() + t * (b.y() - a.y());
        }
        QPointF cur = toWidget(QPointF(x, y));
        if (i > 0) p.drawLine(prev, cur);
        prev = cur;
    }

    // Control points.
    p.setBrush(QColor(120, 180, 255));
    p.setPen(Qt::white);
    for (const QPointF &pt : m_points)
        p.drawEllipse(toWidget(pt), 4, 4);
}

void CurveEditor::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::RightButton) {
        int i = nearestPoint(ev->pos(), kPickDist);
        if (i > 0 && i < m_points.size() - 1) { // keep endpoints
            m_points.remove(i);
            update();
            emit curveChanged(m_points);
        }
        return;
    }
    if (ev->button() != Qt::LeftButton) return;

    int i = nearestPoint(ev->pos(), kPickDist);
    if (i < 0) {
        QPointF c = toCurve(ev->pos());
        m_points.append(c);
        std::sort(m_points.begin(), m_points.end(),
                  [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        i = m_points.indexOf(c);
    }
    m_dragIndex = i;
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent *ev) {
    if (m_dragIndex < 0) return;
    QPointF c = toCurve(ev->pos());
    bool endpoint = (m_dragIndex == 0 || m_dragIndex == m_points.size() - 1);
    if (endpoint) c.setX(m_points[m_dragIndex].x()); // lock endpoints' x
    // Keep x within neighbours to preserve monotonicity.
    if (!endpoint) {
        double lo = m_points[m_dragIndex - 1].x() + 1e-3;
        double hi = m_points[m_dragIndex + 1].x() - 1e-3;
        c.setX(std::clamp(c.x(), lo, hi));
    }
    m_points[m_dragIndex] = c;
    update();
    emit curveChanged(m_points);
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *) {
    m_dragIndex = -1;
}
