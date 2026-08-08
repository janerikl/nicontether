#include "svg/SvgCanvas.h"

#include <QInputDialog>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>

#include "svg/SvgRender.h"

namespace {
QPainterPath nodeLocalPath(const SvgNode &node) { return svgNodeLocalPath(node); }
QTransform nodeTransform(const SvgNode &node) { return svgNodeTransform(node); }
QPainterPath nodeWorldPath(const SvgNode &node) { return svgNodeWorldPath(node); }
} // namespace

SvgCanvas::SvgCanvas(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(60, 60, 62));
    setPalette(pal);
}

void SvgCanvas::setSelection(const QSet<QString> &ids) {
    m_selection = ids;
    emit selectionChanged();
    update();
}

void SvgCanvas::selectSingle(const QString &id) {
    m_selection.clear();
    if (!id.isEmpty()) m_selection.insert(id);
    emit selectionChanged();
    update();
}

void SvgCanvas::clearSelection() {
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    emit selectionChanged();
    update();
}

void SvgCanvas::setZoom(qreal z) {
    m_zoom = std::clamp(z, 0.05, 32.0);
    update();
}

void SvgCanvas::setToolMode(SvgToolMode mode) {
    if (m_toolMode == mode) return;
    if (m_toolMode == SvgToolMode::Pen && m_penActive) finishPenPath();
    m_toolMode = mode;
    emit toolModeChanged(mode);
    update();
}

void SvgCanvas::finishPenPath() {
    if (!m_penActive) return;
    m_penActive = false;
    if (m_penPath.elementCount() >= 2) {
        SvgNode node;
        node.type = SvgNodeType::Path;
        node.name = "Path";
        node.fillType = SvgFillType::None;
        node.strokeEnabled = true;
        node.strokeColor = Qt::black;
        node.strokeWidth = 2.0;
        node.path = m_penPath;
        QString id = m_doc.addNode(node);
        selectSingle(id);
        emit documentChanged();
    }
    m_penPath = QPainterPath();
    update();
}

QPointF SvgCanvas::originOffset() const {
    // Center the document canvas within the widget.
    qreal w = m_doc.canvasSize.width() * m_zoom;
    qreal h = m_doc.canvasSize.height() * m_zoom;
    return QPointF((width() - w) / 2.0, (height() - h) / 2.0) + m_panOffset;
}

QPointF SvgCanvas::toDocument(const QPoint &widgetPos) const {
    QPointF origin = originOffset();
    return QPointF((widgetPos.x() - origin.x()) / m_zoom, (widgetPos.y() - origin.y()) / m_zoom);
}

QPoint SvgCanvas::toWidget(const QPointF &docPos) const {
    QPointF origin = originOffset();
    return QPoint(qRound(origin.x() + docPos.x() * m_zoom), qRound(origin.y() + docPos.y() * m_zoom));
}

void SvgCanvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPointF origin = originOffset();
    QRectF canvasRect(origin, QSizeF(m_doc.canvasSize.width() * m_zoom, m_doc.canvasSize.height() * m_zoom));

    // Checkerboard for transparency, then the document background.
    painter.fillRect(canvasRect, QColor(90, 90, 92));
    if (m_doc.backgroundColor.alpha() > 0)
        painter.fillRect(canvasRect, m_doc.backgroundColor);
    painter.setPen(QPen(QColor(20, 20, 20), 1));
    painter.drawRect(canvasRect);

    painter.save();
    painter.translate(origin);
    painter.scale(m_zoom, m_zoom);
    for (const SvgNode &node : m_doc.nodes) {
        if (!node.visible || node.isGroup()) continue;
        paintNode(painter, node);
    }
    painter.restore();

    paintSelectionHandles(painter);
    paintPenPreview(painter);
}

void SvgCanvas::paintPenPreview(QPainter &painter) const {
    if (!m_penActive || m_penPath.isEmpty()) return;
    painter.save();
    painter.setPen(QPen(QColor(80, 160, 255), 1.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    QPainterPath widgetPath;
    QPointF origin = originOffset();
    const auto elems = m_penPath.toSubpathPolygons();
    for (const QPolygonF &poly : elems) {
        QPolygonF widgetPoly;
        for (const QPointF &p : poly) widgetPoly << (origin + p * m_zoom);
        painter.drawPolyline(widgetPoly);
        for (const QPointF &p : widgetPoly)
            painter.drawEllipse(p, 3, 3);
    }
    painter.restore();
}

void SvgCanvas::paintNode(QPainter &painter, const SvgNode &node) const {
    painter.save();
    painter.setOpacity(node.opacity);
    painter.setTransform(nodeTransform(node), true);

    if (node.type == SvgNodeType::Text) {
        painter.setFont(node.font);
        painter.setPen(node.fillType == SvgFillType::Solid ? node.fillColor : QColor(Qt::black));
        painter.drawText(node.textOrigin, node.text);
        painter.restore();
        return;
    }

    QPainterPath path = nodeLocalPath(node);

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

void SvgCanvas::paintSelectionHandles(QPainter &painter) const {
    if (m_selection.isEmpty()) return;
    painter.save();
    painter.setPen(QPen(QColor(80, 160, 255), 1.5));
    painter.setBrush(Qt::NoBrush);
    for (const QString &id : m_selection) {
        const SvgNode *node = m_doc.findById(id);
        if (!node) continue;
        QPolygonF worldPoly;
        if (node->isGroup()) {
            QRectF unionBounds;
            for (const QString &childId : node->childIds) {
                if (const SvgNode *child = m_doc.findById(childId)) {
                    QRectF b = nodeTransform(*child).mapRect(child->localBounds());
                    unionBounds = unionBounds.isNull() ? b : unionBounds.united(b);
                }
            }
            worldPoly = QPolygonF(unionBounds);
        } else {
            worldPoly = nodeTransform(*node).map(QPolygonF(node->localBounds()));
        }
        QPolygonF widgetPoly;
        for (const QPointF &p : worldPoly) widgetPoly << toWidget(p);
        painter.drawPolygon(widgetPoly);
        painter.setBrush(QColor(80, 160, 255));
        for (const QPointF &p : widgetPoly)
            painter.drawRect(QRectF(p.x() - 4, p.y() - 4, 8, 8));
        painter.setBrush(Qt::NoBrush);
    }
    painter.restore();
}

QString SvgCanvas::hitTest(const QPointF &docPos) const {
    for (int i = m_doc.nodes.size() - 1; i >= 0; --i) {
        const SvgNode &node = m_doc.nodes[i];
        if (!node.visible || node.isGroup()) continue;
        QPointF localPos = nodeTransform(node).inverted().map(docPos);
        if (node.type == SvgNodeType::Text) {
            if (node.localBounds().contains(localPos)) return node.id;
            continue;
        }
        QPainterPath path = nodeLocalPath(node);
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(4.0, node.strokeWidth));
        if (path.contains(localPos) || stroker.createStroke(path).contains(localPos))
            return node.id;
    }
    return QString();
}

namespace {
SvgNodeType shapeNodeTypeFor(SvgToolMode mode) {
    switch (mode) {
    case SvgToolMode::Rect: return SvgNodeType::Rect;
    case SvgToolMode::Ellipse: return SvgNodeType::Ellipse;
    case SvgToolMode::Line: return SvgNodeType::Line;
    case SvgToolMode::Polygon: return SvgNodeType::Polygon;
    case SvgToolMode::Star: return SvgNodeType::Star;
    default: return SvgNodeType::Rect;
    }
}
} // namespace

void SvgCanvas::mousePressEvent(QMouseEvent *event) {
    QPointF docPos = toDocument(event->pos());

    if (m_toolMode == SvgToolMode::Pen) {
        if (!m_penActive) {
            m_penActive = true;
            m_penPath = QPainterPath();
            m_penPath.moveTo(docPos);
        } else {
            m_penPath.lineTo(docPos);
        }
        m_penLastPoint = docPos;
        update();
        return;
    }

    if (m_toolMode == SvgToolMode::Text) {
        bool ok = false;
        QString text = QInputDialog::getText(this, "Add Text", "Text:", QLineEdit::Normal, "Text", &ok);
        if (ok && !text.isEmpty()) {
            SvgNode node;
            node.type = SvgNodeType::Text;
            node.name = "Text";
            node.text = text;
            node.position = docPos;
            node.fillType = SvgFillType::Solid;
            node.fillColor = Qt::black;
            QString id = m_doc.addNode(node);
            selectSingle(id);
            emit documentChanged();
        }
        setToolMode(SvgToolMode::Select);
        return;
    }

    if (m_toolMode != SvgToolMode::Select) {
        m_creatingShape = true;
        m_shapeStartDoc = docPos;
        SvgNode node;
        node.type = shapeNodeTypeFor(m_toolMode);
        node.name = "Shape";
        node.fillType = SvgFillType::Solid;
        node.fillColor = QColor(120, 170, 230);
        if (node.type == SvgNodeType::Rect || node.type == SvgNodeType::Ellipse) {
            node.rect = QRectF(docPos, QSizeF(1, 1));
        } else if (node.type == SvgNodeType::Line) {
            node.strokeEnabled = true;
            node.strokeColor = Qt::black;
            node.strokeWidth = 2.0;
            node.lineP1 = docPos;
            node.lineP2 = docPos;
        } else {
            node.position = docPos;
            node.polygonRadius = 1.0;
        }
        QString id = m_doc.addNode(node);
        selectSingle(id);
        emit documentChanged();
        return;
    }

    QString hitId = hitTest(docPos);

    if (hitId.isEmpty()) {
        clearSelection();
        return;
    }

    if (event->modifiers() & Qt::ShiftModifier) {
        QSet<QString> sel = m_selection;
        if (sel.contains(hitId)) sel.remove(hitId);
        else sel.insert(hitId);
        setSelection(sel);
    } else if (!m_selection.contains(hitId)) {
        selectSingle(hitId);
    }

    m_dragging = true;
    m_dragStartDoc = docPos;
    m_dragStartPositions.clear();
    for (const QString &id : dragTargetIds()) {
        if (const SvgNode *node = m_doc.findById(id))
            m_dragStartPositions << node->position;
    }
}

void SvgCanvas::mouseMoveEvent(QMouseEvent *event) {
    QPointF docPos = toDocument(event->pos());

    if (m_toolMode == SvgToolMode::Pen) {
        update();
        return;
    }

    if (m_creatingShape && !m_selection.isEmpty()) {
        SvgNode *node = m_doc.findById(*m_selection.begin());
        if (node) {
            if (node->type == SvgNodeType::Rect || node->type == SvgNodeType::Ellipse) {
                node->rect = QRectF(m_shapeStartDoc, docPos).normalized();
            } else if (node->type == SvgNodeType::Line) {
                node->lineP2 = docPos;
            } else {
                qreal dx = docPos.x() - m_shapeStartDoc.x();
                qreal dy = docPos.y() - m_shapeStartDoc.y();
                node->polygonRadius = std::hypot(dx, dy);
            }
            emit documentChanged();
            update();
        }
        return;
    }

    if (!m_dragging) return;
    QPointF delta = docPos - m_dragStartDoc;

    int i = 0;
    for (const QString &id : dragTargetIds()) {
        if (SvgNode *node = m_doc.findById(id)) {
            node->position = m_dragStartPositions[i] + delta;
        }
        ++i;
    }
    emit documentChanged();
    update();
}

QVector<QString> SvgCanvas::dragTargetIds() const {
    QVector<QString> ids;
    for (const QString &id : m_selection) {
        const SvgNode *node = m_doc.findById(id);
        if (node && node->isGroup()) {
            for (const QString &childId : node->childIds) ids << childId;
        } else {
            ids << id;
        }
    }
    return ids;
}

void SvgCanvas::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = false;
    if (m_creatingShape) {
        m_creatingShape = false;
        setToolMode(SvgToolMode::Select);
    }
}

void SvgCanvas::mouseDoubleClickEvent(QMouseEvent *) {
    if (m_toolMode == SvgToolMode::Pen && m_penActive) finishPenPath();
}

void SvgCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_penActive) {
            m_penActive = false;
            m_penPath = QPainterPath();
            update();
        } else {
            clearSelection();
        }
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_penActive) finishPenPath();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !m_selection.isEmpty()) {
        for (const QString &id : m_selection) m_doc.removeNode(id);
        clearSelection();
        emit documentChanged();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SvgCanvas::wheelEvent(QWheelEvent *event) {
    qreal factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
    setZoom(m_zoom * factor);
}

void SvgCanvas::applyBooleanOp(SvgBooleanOp op) {
    if (m_selection.size() < 2) return;

    // Combine in document paint order so the result is deterministic.
    QVector<int> indices;
    for (const QString &id : m_selection) {
        int idx = m_doc.indexOfId(id);
        if (idx >= 0) indices << idx;
    }
    if (indices.size() < 2) return;
    std::sort(indices.begin(), indices.end());

    QPainterPath combined = nodeWorldPath(m_doc.nodes[indices.first()]);
    int insertAt = indices.first();
    QColor fillColor = m_doc.nodes[indices.first()].fillColor;

    for (int i = 1; i < indices.size(); ++i) {
        QPainterPath next = nodeWorldPath(m_doc.nodes[indices[i]]);
        switch (op) {
        case SvgBooleanOp::Union: combined = combined.united(next); break;
        case SvgBooleanOp::Subtract: combined = combined.subtracted(next); break;
        case SvgBooleanOp::Intersect: combined = combined.intersected(next); break;
        case SvgBooleanOp::Xor: combined = combined.united(next).subtracted(combined.intersected(next)); break;
        }
    }

    // Remove originals, highest index first so earlier indices stay valid.
    QVector<QString> idsToRemove;
    for (int idx : indices) idsToRemove << m_doc.nodes[idx].id;
    for (const QString &id : idsToRemove) m_doc.removeNode(id);

    SvgNode node;
    node.type = SvgNodeType::Path;
    node.name = "Combined Path";
    node.fillType = SvgFillType::Solid;
    node.fillColor = fillColor;
    node.path = combined; // already in document (world) space; position stays identity.
    insertAt = std::clamp(insertAt, 0, int(m_doc.nodes.size()));
    QString newId = node.id;
    m_doc.nodes.insert(insertAt, node);

    selectSingle(newId);
    emit documentChanged();
    update();
}

void SvgCanvas::groupSelection() {
    if (m_selection.size() < 2) return;
    SvgNode group;
    group.type = SvgNodeType::Group;
    group.name = "Group";
    for (const QString &id : m_selection) {
        group.childIds << id;
        if (SvgNode *child = m_doc.findById(id)) child->parentGroupId = group.id;
    }
    QString groupId = m_doc.addNode(group);
    selectSingle(groupId);
    emit documentChanged();
    update();
}

void SvgCanvas::ungroupSelection() {
    QSet<QString> newSelection;
    for (const QString &id : m_selection) {
        SvgNode *group = m_doc.findById(id);
        if (!group || !group->isGroup()) {
            newSelection.insert(id);
            continue;
        }
        for (const QString &childId : group->childIds) {
            if (SvgNode *child = m_doc.findById(childId)) child->parentGroupId.clear();
            newSelection.insert(childId);
        }
        m_doc.removeNode(id);
    }
    setSelection(newSelection);
    emit documentChanged();
    update();
}

void SvgCanvas::alignSelection(SvgAlignEdge edge) {
    if (m_selection.size() < 2) return;

    QRectF unionBounds;
    for (const QString &id : m_selection) {
        const SvgNode *node = m_doc.findById(id);
        if (!node) continue;
        QRectF worldBounds = nodeTransform(*node).mapRect(node->localBounds());
        unionBounds = unionBounds.isNull() ? worldBounds : unionBounds.united(worldBounds);
    }

    for (const QString &id : m_selection) {
        SvgNode *node = m_doc.findById(id);
        if (!node) continue;
        QRectF worldBounds = nodeTransform(*node).mapRect(node->localBounds());
        QPointF delta(0, 0);
        switch (edge) {
        case SvgAlignEdge::Left: delta.setX(unionBounds.left() - worldBounds.left()); break;
        case SvgAlignEdge::HCenter: delta.setX(unionBounds.center().x() - worldBounds.center().x()); break;
        case SvgAlignEdge::Right: delta.setX(unionBounds.right() - worldBounds.right()); break;
        case SvgAlignEdge::Top: delta.setY(unionBounds.top() - worldBounds.top()); break;
        case SvgAlignEdge::VCenter: delta.setY(unionBounds.center().y() - worldBounds.center().y()); break;
        case SvgAlignEdge::Bottom: delta.setY(unionBounds.bottom() - worldBounds.bottom()); break;
        }
        node->position += delta;
    }
    emit documentChanged();
    update();
}

void SvgCanvas::setFillColor(const QColor &color) {
    for (const QString &id : m_selection) {
        if (SvgNode *node = m_doc.findById(id)) {
            node->fillType = SvgFillType::Solid;
            node->fillColor = color;
        }
    }
    emit documentChanged();
    update();
}

void SvgCanvas::setFillGradient(const SvgGradient &gradient) {
    for (const QString &id : m_selection) {
        if (SvgNode *node = m_doc.findById(id)) {
            node->fillType = SvgFillType::Gradient;
            node->fillGradient = gradient;
        }
    }
    emit documentChanged();
    update();
}

void SvgCanvas::setStroke(bool enabled, const QColor &color, qreal width) {
    for (const QString &id : m_selection) {
        if (SvgNode *node = m_doc.findById(id)) {
            node->strokeEnabled = enabled;
            node->strokeColor = color;
            node->strokeWidth = width;
        }
    }
    emit documentChanged();
    update();
}
