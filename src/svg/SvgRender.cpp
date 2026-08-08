#include "svg/SvgRender.h"

#include <cmath>

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

namespace {
QPainterPath polygonPath(const SvgNode &node) {
    QPainterPath path;
    int sides = std::max(3, node.polygonSides);
    bool isStar = node.type == SvgNodeType::Star;
    int pointCount = isStar ? sides * 2 : sides;
    for (int i = 0; i < pointCount; ++i) {
        qreal angle = (M_PI * 2.0 * i) / pointCount - M_PI_2;
        qreal radius = node.polygonRadius;
        if (isStar && (i % 2 == 1)) radius *= node.starInnerRadiusRatio;
        QPointF p(std::cos(angle) * radius, std::sin(angle) * radius);
        if (i == 0) path.moveTo(p);
        else path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}
} // namespace

QPainterPath svgNodeLocalPath(const SvgNode &node) {
    switch (node.type) {
    case SvgNodeType::Rect: {
        QPainterPath path;
        if (node.rectCornerRadius > 0.0)
            path.addRoundedRect(node.rect, node.rectCornerRadius, node.rectCornerRadius);
        else
            path.addRect(node.rect);
        return path;
    }
    case SvgNodeType::Ellipse: {
        QPainterPath path;
        path.addEllipse(node.rect);
        return path;
    }
    case SvgNodeType::Line: {
        QPainterPath path;
        path.moveTo(node.lineP1);
        path.lineTo(node.lineP2);
        return path;
    }
    case SvgNodeType::Polygon:
    case SvgNodeType::Star:
        return polygonPath(node);
    case SvgNodeType::Path:
        return node.path;
    case SvgNodeType::Text:
    case SvgNodeType::Group:
        return QPainterPath();
    }
    return QPainterPath();
}

QTransform svgNodeTransform(const SvgNode &node) {
    QTransform t;
    t.translate(node.position.x(), node.position.y());
    t.rotate(node.rotationDegrees);
    t.scale(node.scaleX, node.scaleY);
    return t;
}

QPainterPath svgNodeWorldPath(const SvgNode &node) {
    return svgNodeTransform(node).map(svgNodeLocalPath(node));
}

void renderSvgDocument(QPainter &painter, const SvgDocument &doc) {
    for (const SvgNode &node : doc.nodes) {
        if (!node.visible || node.isGroup()) continue;

        painter.save();
        painter.setOpacity(node.opacity);
        painter.setTransform(svgNodeTransform(node), true);

        if (node.type == SvgNodeType::Text) {
            painter.setFont(node.font);
            painter.setPen(node.fillType == SvgFillType::Solid ? node.fillColor : QColor(Qt::black));
            painter.drawText(node.textOrigin, node.text);
            painter.restore();
            continue;
        }

        QPainterPath path = svgNodeLocalPath(node);

        if (node.fillType == SvgFillType::Solid) {
            painter.setBrush(node.fillColor);
        } else if (node.fillType == SvgFillType::Gradient) {
            QRectF bounds = node.localBounds();
            if (node.fillGradient.type == SvgGradientType::Linear) {
                QLinearGradient grad(
                    bounds.topLeft() + QPointF(node.fillGradient.linearStart.x() * bounds.width(),
                                                node.fillGradient.linearStart.y() * bounds.height()),
                    bounds.topLeft() + QPointF(node.fillGradient.linearEnd.x() * bounds.width(),
                                                node.fillGradient.linearEnd.y() * bounds.height()));
                for (const auto &stop : node.fillGradient.stops)
                    grad.setColorAt(stop.position, stop.color);
                painter.setBrush(grad);
            } else {
                QPointF center = bounds.topLeft() + QPointF(node.fillGradient.radialCenter.x() * bounds.width(),
                                                              node.fillGradient.radialCenter.y() * bounds.height());
                QPointF focal = bounds.topLeft() + QPointF(node.fillGradient.radialFocal.x() * bounds.width(),
                                                             node.fillGradient.radialFocal.y() * bounds.height());
                QRadialGradient grad(center, node.fillGradient.radialRadius * std::max(bounds.width(), bounds.height()), focal);
                for (const auto &stop : node.fillGradient.stops)
                    grad.setColorAt(stop.position, stop.color);
                painter.setBrush(grad);
            }
        } else {
            painter.setBrush(Qt::NoBrush);
        }

        if (node.strokeEnabled) {
            QPen pen(node.strokeColor, node.strokeWidth);
            if (!node.strokeDashPattern.isEmpty()) pen.setDashPattern(node.strokeDashPattern);
            painter.setPen(pen);
        } else {
            painter.setPen(Qt::NoPen);
        }

        painter.drawPath(path);
        painter.restore();
    }
}

QImage renderSvgDocumentToImage(const SvgDocument &doc, qreal scale) {
    QSize pixelSize(std::max(1, qRound(doc.canvasSize.width() * scale)),
                     std::max(1, qRound(doc.canvasSize.height() * scale)));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(doc.backgroundColor.alpha() > 0 ? doc.backgroundColor : Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    renderSvgDocument(painter, doc);
    painter.end();
    return image;
}
