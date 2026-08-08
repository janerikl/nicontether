#pragma once

#include <QPointF>
#include <QSet>
#include <QString>
#include <QWidget>

#include "svg/SvgDocument.h"

enum class SvgToolMode { Select, Rect, Ellipse, Line, Polygon, Star, Text, Pen };
enum class SvgBooleanOp { Union, Subtract, Intersect, Xor };
enum class SvgAlignEdge { Left, HCenter, Right, Top, VCenter, Bottom };

// Central editing surface of SvgEditorWindow. Modeled on ImageCanvas
// (src/edit/ImageCanvas.h): a plain QWidget that paints its document with
// QPainter in paintEvent rather than using QGraphicsScene.
//
// Owns the default select/move/transform interaction directly (there is no
// separate "SvgSelectTool" class — selection is the canvas's baseline mode
// that other tools temporarily suspend while active).
class SvgCanvas : public QWidget {
    Q_OBJECT

public:
    explicit SvgCanvas(QWidget *parent = nullptr);

    SvgDocument &document() { return m_doc; }
    const SvgDocument &document() const { return m_doc; }

    const QSet<QString> &selection() const { return m_selection; }
    void setSelection(const QSet<QString> &ids);
    void selectSingle(const QString &id);
    void clearSelection();

    // Document <-> widget coordinate mapping, honoring pan/zoom.
    QPointF toDocument(const QPoint &widgetPos) const;
    QPoint toWidget(const QPointF &docPos) const;

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal z);

    SvgToolMode toolMode() const { return m_toolMode; }
    void setToolMode(SvgToolMode mode);

    // Pen tool: finishes the in-progress path (double-click also does this).
    void finishPenPath();

    // Combines the selected nodes' outlines into a single Path node using
    // QPainterPath's built-in boolean operators. Requires 2+ selected nodes.
    void applyBooleanOp(SvgBooleanOp op);

    // Wraps the selected nodes in a new Group node.
    void groupSelection();
    // Dissolves the selected Group node(s), restoring their children to
    // top-level selection.
    void ungroupSelection();

    // Aligns all selected nodes to the shared bounding box of the selection.
    void alignSelection(SvgAlignEdge edge);

    void setFillColor(const QColor &color);
    void setFillGradient(const SvgGradient &gradient);
    void setStroke(bool enabled, const QColor &color, qreal width);

signals:
    void selectionChanged();
    void documentChanged();
    void toolModeChanged(SvgToolMode mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void paintNode(class QPainter &painter, const SvgNode &node) const;
    void paintSelectionHandles(class QPainter &painter) const;
    QString hitTest(const QPointF &docPos) const;
    QPointF originOffset() const;
    void paintPenPreview(class QPainter &painter) const;
    // Expands any selected Group ids into their child ids (groups have no
    // visual geometry of their own to move); leaves non-group ids as-is.
    QVector<QString> dragTargetIds() const;

    SvgDocument m_doc;
    QSet<QString> m_selection;

    qreal m_zoom = 1.0;
    QPointF m_panOffset{0.0, 0.0};

    bool m_dragging = false;
    QPointF m_dragStartDoc;
    QVector<QPointF> m_dragStartPositions;

    SvgToolMode m_toolMode = SvgToolMode::Select;

    // Shape-drag-to-create state (Rect/Ellipse/Line/Polygon/Star).
    bool m_creatingShape = false;
    QPointF m_shapeStartDoc;

    // Pen tool: path under construction.
    QPainterPath m_penPath;
    bool m_penActive = false;
    QPointF m_penLastPoint;
};
