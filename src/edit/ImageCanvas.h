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
#include <QPainterPath>
#include <QPolygonF>

#include "edit/Adjustments.h"
#include "ui/GridOverlay.h"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QContextMenuEvent;
class QPlainTextEdit;
class QPainter;
class QTabletEvent;

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

    // already-adjusted image to show. `dirtyRect` (image pixel space): when
    // valid and the image size/format hasn't changed, only that region is
    // re-dithered into the displayed 8-bit buffer and repainted on screen,
    // instead of the whole image - see the doc comment on setImage()'s
    // definition. Leave at the default (invalid QRect) to redo everything,
    // as before.
    void setImage(const QImage &img, const QRect &dirtyRect = QRect());
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
    void setBucketMode(bool on); // paint bucket: single click flood-fills the active Paint layer

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

    // Move tool: drag an existing shape/text body or image-layer frame to
    // move it (reuses their own ShapeDrag::Moving/TextDrag::Moving/image-
    // layer-drag paths), or drag on a Paint/Brush layer's content to
    // translate its stroke points (and fill, if unselected) — clipped to the
    // active selection when one exists.
    void setMoveMode(bool on);
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

    // Selection tools: build/replace an active selection region that other
    // tools (brush/pen, erase, paint bucket) clip their writes to. All three
    // are mutually exclusive with each other and with every other tool mode,
    // same as setHealMode/setShapeMode/etc. The active selection itself
    // persists across tool switches until cleared.
    void setSelectMarqueeMode(bool on); // drag a rectangle
    void setSelectLassoMode(bool on);   // drag a freehand polygon
    void setSelectMagicWandMode(bool on); // click: flood-fill by color similarity
    void setMagicWandTolerance(int tolerance); // 0..255 per-channel distance
    // Selection Brush: drag paints a brush-sized dab that's continuously
    // unioned into (or, with Alt held, subtracted from) the active selection
    // as you drag — Photoshop's Quick Selection/Selection Brush equivalent.
    void setSelectBrushMode(bool on);
    void setSelectBrushRadius(double normRadius); // width-normalized, same convention as brushRadius
    // Clears the active selection (Deselect action / Esc).
    void clearActiveSelection();
    void invertSelection();
    bool hasActiveSelection() const { return m_hasSelection; }
    // Current selection region, width-normalized (same convention as
    // maskBrushPoint/maskRadialDragged etc: both axes divided by image width).
    QPainterPath selectionPathNorm() const { return m_selectionPath; }

    // Feather (Photoshop's Select > Feather): softens the selection edge over
    // this many width-normalized units instead of clipping with a hard edge.
    // Applied when the selection is baked into a layer (see Mask::
    // selectionFeatherNorm in RetouchTab), not to the selection outline shown
    // on canvas, which stays a crisp marching-ants path either way.
    void setSelectionFeather(double normRadius);
    double selectionFeatherNorm() const { return m_selectionFeatherNorm; }

    // Clone stamp: Alt+click sets the source point; subsequent drag strokes
    // sample from source + (current - firstDragPoint), same convention as
    // Photoshop. Respects the active selection as a clip (enforced by
    // RetouchTab, same as Brush/Erase).
    void setCloneMode(bool on);

    // Click-to-select fallback: bounding rects (same display-image pixel
    // space as ShapeMarker/TextMarker) for Paint layers and image layers,
    // used only when no tool-specific handler above claims a click (see
    // mousePressEvent). Index-aligned with RetouchTab's own
    // m_paintMaskIndices/m_imageLayerMaskIndices.
    void setPaintMarkers(const QVector<QRectF> &markers);
    void setImageLayerMarkers(const QVector<QRectF> &markers);

    // Local-mask editing. When a mask kind is set, dragging on the canvas
    // defines/updates the active mask's geometry (radial: centre→radius,
    // linear: p0→p1, brush: appends stroke points). setActiveMask supplies the
    // mask to draw as a gizmo (geometry is width-normalized, matching the
    // pipeline). Coordinates are emitted width-normalized.
    void setMaskMode(MaskType kind, bool on);
    // Forces brush strokes to erase coverage (as if Alt were held) regardless
    // of the actual Alt modifier state. Used to drive the E toolbar toggle on
    // a Paint-type mask, reusing the same subtract-coverage stroke path as
    // Alt+drag rather than a separate erase mechanism.
    void setMaskForceErase(bool on);
    void setActiveMask(bool has, const Mask &m);
    // Drops this canvas's shared reference to the active mask's stroke
    // history just before the caller appends a new point to its own copy.
    // Without this, m_activeMask.stroke stays a live QVector COW-share of
    // the document's stroke vector between dabs, so every append() during a
    // drawing stroke forces Qt to deep-copy the entire stroke-so-far before
    // adding the one new point — turning an O(1) append into O(n) and a full
    // stroke into O(n^2), which is what made long pen/paint strokes
    // progressively laggier. Call this right before appending.
    void releaseActiveMaskStroke();
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
    // Pans (without changing zoom) so imagePt — in the same display-image
    // space as ShapeMarker/setImage() — lands in the centre of the viewport.
    void centerOnImagePoint(const QPointF &imagePt);

    // Canvas background (right-click the canvas background to change it).
    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }

    // When on, the photo rect is painted with a Photoshop-style checkerboard
    // instead of the solid background color, showing through transparent
    // pixels left by a hidden/deleted base layer.
    void setShowCheckerboard(bool on);

    // Photoshop-style rulers along the top and left edges of the canvas,
    // showing image-pixel coordinates at the current zoom/pan.
    void setShowRulers(bool on);

    // Composition guide overlay drawn over the full image, independent of
    // crop mode. GridMode::Off disables it.
    void setCompositionGrid(GridMode g);
    GridMode compositionGrid() const { return m_compositionGrid; }

    // Photoshop-style ruler guides: dragged out from the ruler bands.
    // Positions are fractions of the displayed image (0..1) — guidesH of
    // height, guidesV of width — so they survive image-size changes
    // (crop/rotate) reasonably. Only interactive while rulers are shown.
    void setGuides(const QVector<double> &horizontal, const QVector<double> &vertical);

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
    // Right-click-and-hold on the canvas while a paint tool (Brush/Pen/
    // Bucket) is active: a round HSV wheel appears at the press point (see
    // paintEvent), and this fires once on release with whatever color was
    // under the cursor.
    void quickColorPicked(const QColor &color);
    void healAt(const QPoint &imagePoint);
    void bucketFillRequested(const QPointF &ptNorm); // width-normalized, same convention as maskBrushPoint
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
    void eraseAt(const QPointF &ptNorm, bool newStroke); // one erase-stroke sample (width-normalized)
    void eraseFinished();                // drag released -> commit history
    void removeObjectAt(const QPointF &ptNorm); // one remove-object stroke sample (width-normalized)
    void removeObjectFinished();                // drag released -> run inpaint + commit history
    void zoomChanged(double percent);
    void healBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while healing
    void eraseBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while erasing
    void removeObjectBrushRadiusChanged(int radiusDisplayPx); // ctrl+wheel resize while remove-object brushing
    void maskBrushRadiusChanged(double radiusNorm); // ctrl+wheel resize while brush-masking
    void selectBrushRadiusChanged(double radiusNorm); // ctrl+wheel resize while selection-brushing
    void imageLayerTransformChanged(const QPointF &offsetNorm, const QPointF &scaleNorm,
                                    bool lockRatio);

    // Mask geometry edits (all points width-normalized).
    void maskRadialDragged(const QPointF &centerNorm, double radiusNorm);
    void maskLinearDragged(const QPointF &p0Norm, const QPointF &p1Norm);
    // One stroke sample. `pressure` is real QTabletEvent::pressure() (0..1)
    // when the sample came from a stylus, 1.0 for mouse input.
    void maskBrushPoint(const QPointF &ptNorm, bool erase, bool newStroke, double pressure);
    void maskEditFinished();                    // drag released → commit history
    void imageLayerDropped(const QString &path); // a photo was dropped in as a layer

    // Guides were added/moved/deleted (drag from ruler, drag existing guide,
    // drag back onto ruler to delete, or right-click delete/clear-all).
    void guidesChanged(const QVector<double> &horizontal, const QVector<double> &vertical);

    // Click-to-select fallback fired from mousePressEvent when no
    // tool-specific handler above claimed the click (see the generic
    // hit-test just before the pan/zoom-marquee block). `markerIndex` is a
    // position within the type-filtered marker list (m_shapeMarkers,
    // m_textMarkers, m_paintMarkers, or m_imageLayerMarkers depending on
    // `type`) — RetouchTab::onObjectClicked maps it back to a real
    // Adjustments::masks index the same way onShapeSelected/onTextSelected do.
    void objectClicked(MaskType type, int markerIndex);

    // Fires whenever the active selection region changes (drag committed,
    // magic-wand click, or Deselect/Esc). `pathNorm` uses the same
    // width-normalized convention as maskBrushPoint/maskRadialDragged.
    void selectionPathChanged(const QPainterPath &pathNorm, bool hasSelection);
    void selectionFeatherChanged(double normRadius);

    // Clone stamp. `ptNorm`/`sourceNorm` are width-normalized, same
    // convention as maskBrushPoint. `sourceSet` fires on Alt+click.
    void cloneSourcePicked(const QPointF &sourceNorm);
    void cloneStrokePoint(const QPointF &ptNorm, const QPointF &sourceNorm, bool newStroke,
                          double pressure);
    void cloneFinished(); // drag released -> commit history

    // Move tool dragging a Paint/Brush layer's content. `markerIndex` is a
    // position within m_paintMarkers (see setPaintMarkers), same convention
    // as objectClicked. deltaNorm is the total width-normalized offset from
    // the drag start (not incremental), matching shapeMoved's convention.
    void paintLayerMoveStarted(int markerIndex);
    void paintLayerMoveDelta(const QPointF &deltaNorm);
    void paintLayerMoveFinished(); // drag released -> commit history

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void tabletEvent(QTabletEvent *) override;
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

    int paintMarkerAt(const QPoint &pos) const; // which Paint-layer bounding box is under pos, or -1
    int imageLayerMarkerAt(const QPoint &pos) const; // which image-layer bounding box is under pos, or -1

    // Selection tools.
    QTransform normToWidgetTransform() const; // inverse of normPointAt, for drawing/hit-testing
    QPainterPath magicWandPath(int seedPx, int seedPy, int tolerance) const;
    // Combines `opPath` (already width-normalized) into m_selectionPath per
    // modifier keys: Shift = add, Alt = subtract, neither = replace.
    void applySelectionOp(const QPainterPath &opPath, Qt::KeyboardModifiers mods);
    // One Selection Brush dab: only the pixels within the brush's circle at
    // `centerNorm` that are color-similar to the pixel under the brush center
    // (same per-channel tolerance concept as the magic wand) are included, so
    // painting near an edge "sticks" to the object instead of also picking up
    // the background just because the circle overlapped it.
    QPainterPath selectBrushDabPath(const QPointF &centerNorm) const;

    void relayoutFit();  // recompute scale/offset to fit + centre
    void zoomTo(double newScale, const QPointF &anchorWidgetPos);
    void clampPan();
    void drawRulers(QPainter &p); // top/left ruler overlay, painted last
    void drawGuides(QPainter &p); // dragged-out guide lines, painted over the image but under rulers
    int guideHAt(const QPoint &pos) const; // index into m_guidesH near widget y, or -1
    int guideVAt(const QPoint &pos) const; // index into m_guidesV near widget x, or -1

    QImage m_img;
    QString m_placeholder = "Decoding…";
    bool m_cropMode = false;
    bool m_pickMode = false;
    bool m_bucketMode = false;

    // Targeted color-range tool state.
    bool m_colorRangeMode = false;
    bool m_colorRangeDragging = false;
    QPoint m_colorRangeStart;   // widget coords of the press
    QColor m_colorRangeColor;   // sampled target color (swatch fill)
    int m_colorRangeChannel = 0; // 0=R,1=G,2=B — dominant channel (swatch border)
    int m_colorRangeAmount = 0;  // -100..100, shown in the amount bar

    // Quick color-wheel picker: right-click-and-hold on the canvas while a
    // paint tool (Brush/Pen/Bucket) is active. The wheel's on-screen
    // position stays fixed at the press point; only the highlighted color
    // (and the indicator dot inside the wheel) tracks the cursor while held.
    static constexpr int kQuickColorWheelRadius = 60; // widget px
    bool m_quickColorPicking = false;
    QPoint m_quickColorCenter;   // widget coords: wheel center, fixed at press point
    QColor m_quickColorPreview;  // live color under the cursor while held
    QImage m_quickColorWheelImg; // cached wheel bitmap, rebuilt once per press
    bool m_suppressNextContextMenu = false; // swallow the native RMB-release context menu after a pick
    QColor quickColorAt(const QPoint &pos) const;

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
    // Click-to-select fallback bounding rects (display-image-space), see
    // setPaintMarkers()/setImageLayerMarkers().
    QVector<QRectF> m_paintMarkers;
    QVector<QRectF> m_imageLayerMarkers;
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

    bool m_moveMode = false;
    enum class MoveDrag { None, Paint };
    MoveDrag m_moveDrag = MoveDrag::None;
    QPointF m_moveDragStartNorm;

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
    double m_brushRadiusAccum = 0.0; // fractional remainder carried between Ctrl+wheel events so small trackpad deltas still add up smoothly instead of rounding to zero

    // Selection tool state.
    bool m_selectMarqueeMode = false;
    bool m_selectLassoMode = false;
    bool m_selectMagicWandMode = false;
    int m_magicWandTolerance = 32; // 0..255 per-channel color distance
    QPainterPath m_selectionPath; // width-normalized, see selectionPathNorm()
    bool m_hasSelection = false;
    double m_selectionFeatherNorm = 0.0; // width-normalized, see selectionFeatherNorm()
    enum class SelectDrag { None, Marquee, Lasso, Brush };
    SelectDrag m_selectDrag = SelectDrag::None;
    QPoint m_selectDragStartWidget, m_selectDragCurrentWidget; // marquee, widget px
    QPolygonF m_lassoPolygonWidget; // lasso, widget px, accumulated while dragging

    // Selection Brush state.
    bool m_selectBrushMode = false;
    double m_selectBrushRadiusNorm = 0.04; // width-normalized dab radius
    bool m_selectBrushSubtract = false;    // Alt held at drag start
    QPainterPath m_selectionAtBrushDragStart; // baseline the drag's dabs are unioned/subtracted against
    QPainterPath m_selectBrushStrokeAccum;    // union of every dab painted so far this drag
    QPointF m_lastSelectBrushNorm{-1, -1};

    // Clone stamp state.
    bool m_cloneMode = false;
    bool m_cloneDragging = false;
    QPointF m_cloneSourceNorm{-1, -1}; // set via Alt+click; (-1,-1) = unset
    QPointF m_cloneOffsetNorm{0, 0};   // source - firstDragPoint, captured at drag start
    QPointF m_lastCloneNorm{-1, -1};   // throttles stroke samples, same pattern as m_lastBrushNorm

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
    bool m_maskForceErase = false; // E toggle: force erase on a Paint-type mask
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
    bool m_showRulers = false;
    GridMode m_compositionGrid = GridMode::Off;
    void drawCompositionGrid(QPainter &p, const QRect &tr); // composition guide overlay over the image rect

    // Ruler guides (view-only overlay). Positions are fractions (0..1) of
    // the displayed image's height (m_guidesH) / width (m_guidesV).
    QVector<double> m_guidesH;
    QVector<double> m_guidesV;
    enum class GuideDrag { None, NewH, NewV, MoveH, MoveV };
    GuideDrag m_guideDrag = GuideDrag::None;
    int m_guideDragIndex = -1; // index into m_guidesH/m_guidesV being moved (MoveH/MoveV only)
    QPoint m_guideDragPos;     // current widget-space position while dragging
    void emitGuidesChanged() { emit guidesChanged(m_guidesH, m_guidesV); }
};
