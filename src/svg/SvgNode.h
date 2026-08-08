#pragma once

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QUuid>

#include "svg/SvgGradient.h"

enum class SvgNodeType { Path, Rect, Ellipse, Line, Polygon, Star, Text, Group };

enum class SvgFillType { None, Solid, Gradient };

// A single element of an SvgDocument. Deliberately a flat struct (mirroring
// the Mask struct used for Retouch layers) rather than a class hierarchy —
// keeps serialization/undo simple for the modest number of node types here.
struct SvgNode {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name = "Layer";
    SvgNodeType type = SvgNodeType::Rect;

    bool visible = true;
    bool locked = false;
    qreal opacity = 1.0;

    // Transform, applied around the node's local origin.
    QPointF position{0.0, 0.0};
    qreal rotationDegrees = 0.0;
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;

    // Fill
    SvgFillType fillType = SvgFillType::Solid;
    QColor fillColor = QColor(200, 200, 200);
    SvgGradient fillGradient;

    // Stroke
    bool strokeEnabled = false;
    QColor strokeColor = Qt::black;
    qreal strokeWidth = 1.0;
    QVector<qreal> strokeDashPattern; // empty = solid

    // Rect
    QRectF rect{0.0, 0.0, 100.0, 100.0};
    qreal rectCornerRadius = 0.0;

    // Ellipse (uses `rect` as bounding box)

    // Line
    QPointF lineP1{0.0, 0.0};
    QPointF lineP2{100.0, 0.0};

    // Polygon / Star
    int polygonSides = 5;
    qreal polygonRadius = 50.0;
    qreal starInnerRadiusRatio = 0.5; // Star only

    // Path (freehand pen tool authoring, and result of boolean ops)
    QPainterPath path;

    // Text
    QString text = "Text";
    QFont font;
    QPointF textOrigin{0.0, 0.0};

    // Group
    QVector<QString> childIds;
    QString parentGroupId; // empty if top-level

    QRectF localBounds() const {
        switch (type) {
        case SvgNodeType::Rect:
        case SvgNodeType::Ellipse:
            return rect;
        case SvgNodeType::Line:
            return QRectF(lineP1, lineP2).normalized();
        case SvgNodeType::Polygon:
        case SvgNodeType::Star:
            return QRectF(-polygonRadius, -polygonRadius, polygonRadius * 2, polygonRadius * 2);
        case SvgNodeType::Path:
            return path.boundingRect();
        case SvgNodeType::Text:
            return QRectF(textOrigin, QSizeF(200, 40)); // refined once font metrics are laid out
        case SvgNodeType::Group:
            return QRectF();
        }
        return QRectF();
    }

    bool isGroup() const { return type == SvgNodeType::Group; }
};
