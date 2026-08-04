#include "edit/ShapeTool.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace {

// N points evenly spaced on the ellipse inscribed in `rect`, vertex 0
// pointing straight up.
QPolygonF regularPolygon(const QRectF &rect, int sides) {
    QPolygonF poly;
    const QPointF c = rect.center();
    const double rx = rect.width() / 2.0, ry = rect.height() / 2.0;
    for (int i = 0; i < sides; ++i) {
        double a = -M_PI / 2.0 + i * 2.0 * M_PI / sides;
        poly << QPointF(c.x() + rx * std::cos(a), c.y() + ry * std::sin(a));
    }
    return poly;
}

// 2N points alternating outer/inner radius, vertex 0 pointing straight up.
QPolygonF starPolygon(const QRectF &rect, int sides, double innerRatio) {
    QPolygonF poly;
    const QPointF c = rect.center();
    const double rx = rect.width() / 2.0, ry = rect.height() / 2.0;
    const int n = sides * 2;
    for (int i = 0; i < n; ++i) {
        double a = -M_PI / 2.0 + i * M_PI / sides;
        double f = (i % 2 == 0) ? 1.0 : innerRatio;
        poly << QPointF(c.x() + rx * f * std::cos(a), c.y() + ry * f * std::sin(a));
    }
    return poly;
}

// Classic parametric heart curve, computed once in a [-1,1]x[-1,1]-ish unit
// space, then mapped into a shape's bounding rect via QTransform.
const QPainterPath &unitHeartPath() {
    static const QPainterPath path = [] {
        QPainterPath p;
        const int steps = 120;
        QPolygonF poly;
        for (int i = 0; i <= steps; ++i) {
            double t = 2.0 * M_PI * i / steps;
            double x = 16.0 * std::pow(std::sin(t), 3);
            double y = -(13.0 * std::cos(t) - 5.0 * std::cos(2 * t) -
                         2.0 * std::cos(3 * t) - std::cos(4 * t));
            poly << QPointF(x, y);
        }
        p.addPolygon(poly);
        p.closeSubpath();
        return p;
    }();
    return path;
}

QPainterPath buildShapePath(const ShapeOp &op) {
    QPainterPath path;
    switch (op.type) {
    case ShapeType::Rectangle:
        path.addRect(op.rect);
        break;
    case ShapeType::Ellipse:
        path.addEllipse(op.rect);
        break;
    case ShapeType::Line:
        break; // drawn directly as a line, no fill path
    case ShapeType::Polygon:
        path.addPolygon(regularPolygon(op.rect, std::clamp(op.sides, 3, 20)));
        path.closeSubpath();
        break;
    case ShapeType::Star:
        path.addPolygon(starPolygon(op.rect, std::clamp(op.sides, 3, 20),
                                     std::clamp(op.innerRadiusRatio, 0.1, 0.9)));
        path.closeSubpath();
        break;
    case ShapeType::Heart: {
        QRectF unitBounds = unitHeartPath().boundingRect();
        QTransform t;
        t.translate(op.rect.left(), op.rect.top());
        t.scale(op.rect.width() / unitBounds.width(), op.rect.height() / unitBounds.height());
        t.translate(-unitBounds.left(), -unitBounds.top());
        path = t.map(unitHeartPath());
        break;
    }
    }
    return path;
}

} // namespace

QRectF shapeOpBounds(const ShapeOp &op) {
    QRectF r = (op.type == ShapeType::Line) ? QRectF(op.p1, op.p2).normalized() : op.rect;
    double pad = op.strokeEnabled ? op.strokeWidth / 2.0 : 0.0;
    return r.adjusted(-pad, -pad, pad, pad);
}

void applyShapeOp(QImage &img, const ShapeOp &op) {
    QPointF pivot = (op.type == ShapeType::Line) ? (op.p1 + op.p2) / 2.0 : op.rect.center();

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(std::clamp(op.opacity, 0.0, 1.0));
    p.translate(pivot);
    p.rotate(op.rotation);
    p.translate(-pivot);

    if (op.type == ShapeType::Line) {
        if (op.strokeEnabled && op.strokeWidth > 0) {
            QPen pen(op.strokeColor, op.strokeWidth);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(op.p1, op.p2);
        }
        return;
    }

    QPainterPath path = buildShapePath(op);
    if (op.fillEnabled) {
        p.setPen(Qt::NoPen);
        p.fillPath(path, op.fillColor);
    }
    if (op.strokeEnabled && op.strokeWidth > 0) {
        QPen pen(op.strokeColor, op.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        p.strokePath(path, pen);
    }
}
