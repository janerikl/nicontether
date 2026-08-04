#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QElapsedTimer>
#include <QSet>
#include <QList>

#include "edit/Adjustments.h"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QContextMenuEvent;
class QPlainTextEdit;

// Displays an image with zoom + pan, and supports crop rubber-band selection and
// a white-balance eyedropper. Zoom: Ctrl+wheel (anchored to the cursor),
// left-drag a marquee box to zoom to a region, Space+drag to pan. All crop/pick
// mapping goes through targetRect(), so it works at any zoom.
class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // An existing spot-heal op, in the same pixel space as the QImage passed
    // to setImage() (i.e. display-scaled, already oriented/cropped).
    struct HealMarker {
        QPointF pos;
        double radius = 0.0;
    };

    void setImage(const QImage &img); // already-adjusted image to show
    void setPlaceholder(const QString &text);
    void setCropMode(bool on);
    void setCropAspect(double widthOverHeight); // 0 = freeform
    void setPickMode(bool on); // white-balance eyedropper
    // Targeted color-range tool (Levels panel): click samples a target color,
    // horizontal drag adjusts it. A swatch of the picked color plus an amount
    // bar is drawn next to the click point while dragging.
    void setColorRangePickMode(bool on);
    void setColorRangeAmount(int amount); // -100..100, shown in the drag swatch
    void setHealMode(bool on); // spot-heal brush
    void setTextMode(bool on); // text tool: click to place, drag to move/rotate

    // An existing text op's on-canvas footprint, in the same pixel space as
    // the QImage passed to setImage() (display-scaled, already
    // oriented/cropped). `rect` is the unrotated bounding box; the vector
    // index corresponds 1:1 to the order of Adjustments::texts.
    struct TextMarker {
        QRectF rect;
        double rotation = 0.0; // degrees, clockwise, about rect.topLeft()
    };
    void setTextMarkers(const QVector<TextMarker> &markers);
    void setActiveTextIndex(int index); // -1 = none; shows the rotate handle

    // Opens the inline editor over the given text op. `imgPos`/`font`/`color`
    // style the editor to match the op while typing.
    void beginTextEdit(int index, const QPointF &imgPos, const QFont &font,
                       const QColor &color, const QString &initialText);
    void setShapeMode(bool on); // shape tool: drag to create, click to select/move/resize/rotate
    void setActiveShapeType(ShapeType t); // type created by the next drag-to-create gesture

    // An existing shape op's on-canvas footprint, in the same pixel space as
    // the QImage passed to setImage() (display-scaled, already
    // oriented/cropped). `rect` is used for all types except Line, which
    // uses `p1`/`p2` instead. Index corresponds 1:1 to Adjustments::shapes.
    struct ShapeMarker {
        ShapeType type = ShapeType::Rectangle;
        QRectF rect;
        QPointF p1, p2;
        double rotation = 0.0; // degrees, clockwise, about rect.center() (or p1/p2 midpoint)
    };
    void setShapeMarkers(const QVector<ShapeMarker> &markers);
    void setActiveShapeIndex(int index); // -1 = none; shows resize/rotate handles
    // Full multi-selection (superset of the active index); every member gets
    // a selection outline, but only the active one gets resize/rotate
    // handles. Pushed down whenever RetouchTab's selection set changes.
    void setSelectedShapeIndices(const QSet<int> &indices);

    void setEraseMode(bool on); // erase brush: punches transparency into the selected image layer
    void setRemoveObjectMode(bool on); // remove-object brush: paint over an unwanted object
    // While a stroke's content-aware fill is being computed on a worker
    // thread, ignore new remove-object presses so a second stroke can't be
    // started until the first finishes (see RetouchTab::onRemoveObjectFinished).
    void setRemoveObjectBusy(bool busy);

    // An existing RemoveObjectOp's on-canvas footprint (its bounding rect),
    // in the same pixel space as the QImage passed to setImage() (display-
    // scaled, already oriented/cropped). Index corresponds 1:1 to
    // Adjustments::removals.
    struct RemovalMarker {
        QRectF rect;
    };
    void setRemovalMarkers(const QVector<RemovalMarker> &markers);
    void setActiveRemovalIndex(int index); // -1 = none; drawn with a highlighted outline

    void setZoomMode(bool on); // zoom tool: enables marquee-drag zoom + Ctrl+wheel
    void setBrushRadius(int displayPx);

    // Local-mask editing. When a mask kind is set, dragging on the canvas
    // defines/updates the active mask's geometry (radial: centre→radius,
    // linear: p0→p1, brush: appends stroke points). setActiveMask supplies the
    // mask to draw as a gizmo (geometry is width-normalized, matching the
    // pipeline). Coordinates are emitted width-normalized.
    void setMaskMode(MaskType kind, bool on);
    void setActiveMask(bool has, const Mask &m);
    // Live coverage preview shown while painting a brush mask, independent of
    // whether any local adjustment sliders have been touched yet.
    // Existing heal spots, shown as a reddish overlay while hovering in heal
    // mode (Lightroom-style "visualize spots"); hidden once the mouse leaves.
    void setHealSpots(const QVector<HealMarker> &spots);
    void clearSelection();

    // Zoom control.
    void zoomFit();
    void setZoomPercent(double percent); // anchored to the view centre
    double zoomPercent() const { return m_scale * 100.0; }

    // Canvas background (right-click the canvas background to change it).
    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }

    // When on, the photo rect is painted with a Photoshop-style checkerboard
    // instead of the solid background color, showing through transparent
    // pixels left by a hidden/deleted base layer.
    void setShowCheckerboard(bool on);

signals:
    void backgroundColorChanged(const QColor &color);
    void cropSelected(const QRect &imageRect, double angleDegrees);
    void commitCropRequested();
    void colorPicked(const QColor &color);

    // Targeted color-range gesture: press samples the pixel, moves report the
    // horizontal delta from the press point, release commits the gesture.
    void colorRangePickStarted(const QColor &color);
    void colorRangeDragged(int dxPixels);
    void colorRangeReleased();
    void healAt(const QPoint &imagePoint);
    void textPlaceRequested(const QPoint &imagePoint);
    void textSelected(int index);
    void textDeselected();
    void textMoved(int index, const QPointF &newImagePos);
    void textRotated(int index, double newRotationDegrees);
    void textEditRequested(int index); // double-click on existing text
    void textEditCommitted(int index, const QString &text);
    void textEditCancelled(int index);
    void textLiveContentChanged(int index, const QString &text); // fires on every keystroke
    void textDeleteRequested(int index);
    void textResizeStarted(int index); // corner-drag resize began
    void textResized(int index, double ratio); // font size = size-at-drag-start * ratio
    void shapeCreateRequested(ShapeType type, const QRectF &imageRect); // drag-to-create bounding box
    void shapeSelected(int index);
    void shapeDeselected();
    void shapeMoved(int index, const QPointF &deltaImage);
    void shapeResized(int index, const QRectF &newImageRect); // unrotated local rect
    void shapeLineEndpointsChanged(int index, const QPointF &p1, const QPointF &p2);
    void shapeRotated(int index, double newRotationDegrees);
    void shapeDeleteRequested(int index);
    void shapeGroupDeleteRequested(const QList<int> &indices); // Delete with a multi-selection
    // Ctrl+drag on an existing shape: duplicate it in place (same geometry
    // and style), select the copy, and continue the drag as a move of the
    // copy — the original is left untouched.
    void shapeDuplicateRequested(int index);
    void shapeGroupDuplicateRequested(const QList<int> &indices); // Ctrl+drag with a multi-selection
    void shapeRaiseRequested(int index); // '+': move one level up the stack
    void shapeLowerRequested(int index); // '-': move one level down the stack
    // Ctrl+click (no drag) on a shape: toggle its multi-selection membership.
    void shapeToggleSelectRequested(int index);
    // Press on a shape that's already part of a >1-member selection: the
    // whole group is about to be dragged together. RetouchTab captures each
    // member's start geometry here (same pattern as shapeSelected does for a
    // single-shape move) so shapeGroupMoveRequested's delta can be applied as
    // an absolute offset instead of compounding across move events.
    void shapeGroupMoveStarted(const QList<int> &indices);
    void shapeGroupMoveRequested(const QList<int> &indices, const QPointF &deltaImage);
    // Dragging a corner handle of the multi-selection's combined bounding
    // box: every selected shape scales together about the fixed opposite
    // corner. `shapeGroupResizeStarted` reuses the same start-capture as a
    // group move (position AND size both need a fixed reference point).
    void shapeGroupResizeStarted(const QList<int> &indices);
    void shapeGroupResizeRequested(const QList<int> &indices, const QPointF &anchorImage,
                                   double scaleX, double scaleY);
    void eraseAt(const QPointF &ptNorm); // one erase-stroke sample (width-normalized)
    void eraseFinished();                // drag released -> commit history
    void removeObjectAt(const QPointF &ptNorm); // one remove-object stroke sample (width-normalized)
    void removeObjectFinished();                // drag released -> run inpaint + commit history
    void zoomChanged(double percent);
    void healBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while healing
    void eraseBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while erasing
    void removeObjectBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while remove-object brushing
    void maskBrushRadiusChanged(double radiusNorm); // ctrl+wheel resize while brush-masking
    void imageLayerTransformChanged(const QPointF &offsetNorm, const QPointF &scaleNorm,
                                    bool lockRatio);

    // Mask geometry edits (all points width-normalized).
    void maskRadialDragged(const QPointF &centerNorm, double radiusNorm);
    void maskLinearDragged(const QPointF &p0Norm, const QPointF &p1Norm);
    void maskBrushPoint(const QPointF &ptNorm, bool erase); // one stroke sample
    void maskEditFinished();                    // drag released → commit history
    void imageLayerDropped(const QString &path); // a photo was dropped in as a layer

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    enum class Drag { None, Creating, Moving, Resizing, Rotating };
    enum class Handle { None, TopLeft, Top, TopRight, Right,
                        BottomRight, Bottom, BottomLeft, Left };

    QRect targetRect() const;          // where the image is painted (zoom+pan)
    QRectF imageLayerFrameRect() const;
    Handle imageLayerHandleAt(const QPoint &pos) const;
    QRect selectionRect() const;       // current rubber band in widget coords, unrotated
    QRect selectionInImage() const;    // current rubber band mapped to image coords
    QPoint constrainedCorner(const QPoint &pos) const; // apply aspect + bounds
    Handle handleAt(const QPoint &pos) const; // which crop handle is under pos
    // True when pos is just outside a corner handle (Photoshop-style rotate
    // ring): inside the handle itself resizes, just beyond it rotates.
    bool cropInRotateZone(const QPoint &pos) const;
    QPointF cropLocalPoint(const QPoint &pos) const; // pos unrotated into selectionRect()'s local frame
    bool cropRectContains(const QPoint &pos) const;
    int textMarkerAt(const QPoint &pos) const; // which text body is under pos, or -1
    QPointF textRotateHandlePos(const TextMarker &m) const; // widget coords
    // Which corner handle (of the active text's box) is under `pos`, or None.
    // TopLeft is excluded — it coincides with the rotation anchor, so a drag
    // there moves the text rather than resizing it.
    Handle textCornerHandleAt(const QPoint &pos) const;
    QPointF textCornerLocal(const TextMarker &m, Handle corner) const; // unrotated local space
    QPointF textCornerScreenPos(const TextMarker &m, Handle corner) const;
    void commitTextEditor();
    void cancelTextEditor();

    int shapeMarkerAt(const QPoint &pos) const; // which shape body is under pos, or -1
    QPointF shapeRotateHandlePos(const ShapeMarker &m) const; // widget coords
    QPointF shapeCornerLocal(const ShapeMarker &m, Handle corner) const; // unrotated local space
    QPointF shapeCornerScreenPos(const ShapeMarker &m, Handle corner) const;
    Handle shapeCornerHandleAt(const QPoint &pos) const; // which corner of the active shape
    QPointF shapeEndpointScreenPos(const ShapeMarker &m, bool first) const; // Line p1/p2
    int shapeEndpointAt(const QPoint &pos) const; // 0=p1, 1=p2, -1=none, for the active Line
    QRectF shapeGroupBounds() const; // union of every selected shape's (rotated) bounding box, marker space
    Handle shapeGroupCornerHandleAt(const QPoint &pos) const; // corner of shapeGroupBounds() under pos

    void relayoutFit();  // recompute scale/offset to fit + centre
    void zoomTo(double newScale, const QPointF &anchorWidgetPos);
    void clampPan();

    QImage m_img;
    QString m_placeholder = "Decoding…";
    bool m_cropMode = false;
    bool m_pickMode = false;

    // Targeted color-range tool state.
    bool m_colorRangeMode = false;
    bool m_colorRangeDragging = false;
    QPoint m_colorRangeStart;   // widget coords of the press
    QColor m_colorRangeColor;   // sampled target color (swatch fill)
    int m_colorRangeChannel = 0; // 0=R,1=G,2=B — dominant channel (swatch border)
    int m_colorRangeAmount = 0;  // -100..100, shown in the amount bar
    bool m_healMode = false;
    bool m_textMode = false;
    QVector<TextMarker> m_textMarkers; // display-image-space, index-aligned with Adjustments::texts
    int m_activeTextIndex = -1;
    enum class TextDrag { None, Moving, Rotating, Resizing };
    TextDrag m_textDrag = TextDrag::None;
    QPointF m_textDragStartImgPos;   // marker rect top-left at drag start (image px)
    QPoint m_textDragStartMouse;     // widget px
    double m_textRotateStartAngle = 0.0; // marker rotation at drag start
    Handle m_textResizeCorner = Handle::None; // which corner is being dragged
    double m_textResizeStartDist = 1.0;       // anchor->corner distance (local px) at drag start
    QPlainTextEdit *m_textEditor = nullptr;
    int m_textEditIndex = -1; // index being edited by m_textEditor, or -1
    // Smart-guide snap targets (image px) matched during a Ctrl+drag move,
    // shown as guide lines while dragging and cleared on release.
    QVector<double> m_activeGuideXs;
    QVector<double> m_activeGuideYs;
    QPointF snapTextPosition(const QPointF &pos, int index); // Ctrl+drag alignment snap
    bool m_shapeMode = false;
    ShapeType m_activeShapeType = ShapeType::Rectangle;
    QVector<ShapeMarker> m_shapeMarkers; // display-image-space, index-aligned with Adjustments::shapes
    int m_activeShapeIndex = -1;
    QSet<int> m_selectedShapeIndices; // multi-selection outline set; superset of m_activeShapeIndex
    enum class ShapeDrag { None, Creating, Moving, MovingGroup, Rotating, Resizing, ResizingGroup, EndpointDrag };
    ShapeDrag m_shapeDrag = ShapeDrag::None;
    QPoint m_shapeDragStartMouse;      // widget px
    QPointF m_shapeDragStartTopLeft;   // marker rect top-left at drag start (image px)
    QPointF m_shapeDragStartP1, m_shapeDragStartP2; // Line endpoints at drag start (image px)
    double m_shapeRotateStartAngle = 0.0;
    Handle m_shapeResizeCorner = Handle::None;
    QSizeF m_shapeResizeStartSize; // active shape's rect size at resize-drag start (for Shift aspect-lock)
    int m_shapeEndpointDragging = -1; // 0=p1, 1=p2, while dragging a Line endpoint
    QPoint m_shapeCreateP0, m_shapeCreateP1; // rubber-band corners while creating (widget coords)

    // Ctrl+press on a shape body is ambiguous until release: a plain click
    // toggles multi-selection, a drag past a small threshold duplicates the
    // shape and continues as a move of the copy (see mousePressEvent).
    bool m_shapeCtrlPending = false;
    int m_shapeCtrlPendingHit = -1;
    bool m_shapeCtrlPendingGroup = false; // true if the pending hit is part of a >1-member selection
    QList<int> m_shapeGroupIndices; // members being dragged together, captured at MovingGroup press
    Handle m_shapeGroupResizeCorner = Handle::None;
    QRectF m_shapeGroupResizeStartBounds; // shapeGroupBounds() at ResizingGroup drag start (marker space)

    bool m_eraseMode = false;
    bool m_eraseDragging = false;
    QPointF m_lastEraseNorm{-1, -1};
    bool m_removeObjectMode = false;
    bool m_removeObjectDragging = false;
    bool m_removeObjectBusy = false; // an inpaint is computing; ignore new presses
    QPointF m_lastRemoveObjectNorm{-1, -1};
    QVector<RemovalMarker> m_removalMarkers; // display-image-space, index-aligned with Adjustments::removals
    int m_activeRemovalIndex = -1;
    bool m_zoomMode = false; // gates marquee-drag zoom + Ctrl+wheel zoom
    int m_brushRadius = 20; // display px, for the brush cursor

    // Local-mask editing state.
    bool m_maskMode = false;
    MaskType m_maskKind = MaskType::Radial;
    bool m_hasActiveMask = false;
    Mask m_activeMask;
    bool m_hasActiveImageLayer = false;
    QImage m_maskOverlay; // cached brush-coverage preview for m_activeMask        // geometry to draw as a gizmo
    BrushRasterCache m_maskOverlayCache; // incremental rasterization cache for m_maskOverlay
    bool m_maskDragging = false;
    bool m_maskErasing = false; // Alt held while brush-masking: erase instead of paint
    QPointF m_maskCenterNorm; // radial centre / linear p0 captured at press
    QPointF m_lastBrushNorm{-1, -1};
    QPointF normPointAt(const QPoint &pos) const; // widget → width-normalized
    QPoint m_mousePos;
    QVector<HealMarker> m_healSpots;
    Drag m_drag = Drag::None;
    double m_cropAspect = 0.0; // width/height; 0 = freeform
    QPoint m_p0, m_p1; // crop selection corners (widget coords)
    QPoint m_moveStart;
    QRect m_rectAtMoveStart;
    Handle m_activeHandle = Handle::None;
    QRect m_rectAtDragStart; // selection rect (widget coords) captured at press
    double m_cropAngle = 0.0;        // degrees, clockwise, about selectionRect().center()
    QPoint m_cropDragStartMouse;     // widget px, captured at Rotating drag start
    double m_cropRotateStartAngle = 0.0; // m_cropAngle at Rotating drag start

    bool m_imageDragging = false;
    Handle m_imageActiveHandle = Handle::None;
    QPoint m_imageMoveStart;
    QRectF m_imageFrameAtDragStart;
    QPointF m_imageOffsetAtDragStart;
    QPointF m_imageScaleAtDragStart;
    // Throttles imageLayerTransformChanged emissions during a drag so the
    // (expensive) model re-render isn't requested faster than ~60fps; the
    // frame/handles themselves still update every move via m_activeMask,
    // which is written locally and doesn't wait on that round trip.
    QElapsedTimer m_imageDragEmitThrottle;

    // Zoom / pan.
    double m_scale = 1.0;   // widget px per image px
    QPointF m_topLeft;      // widget coords of image (0,0)
    bool m_fit = true;      // auto-fit until the user zooms

    // Marquee zoom + pan drags (normal mode).
    bool m_marquee = false;
    QPoint m_mp0, m_mp1;
    bool m_panning = false;
    QPoint m_panLast;
    bool m_spaceDown = false;

    bool m_dragHighlight = false; // a valid image drag is hovering the canvas
    QColor m_backgroundColor = QColor(30, 30, 30);
    bool m_showCheckerboard = false;
};
