// Covers RetouchTab::reorderMasks' defensive permutation check, added after
// a drag that pulled a layer out of a group (which the LayersPanel used to
// silently drop instead of reporting) could desync the tree from
// m_adj.masks and hand reorderMasks a non-bijective order — losing or
// duplicating a mask and leaving m_activeMask dangling past the array.
#include "edit/RetouchTab.h"

#include <QApplication>
#include <cassert>

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // A fresh tab auto-creates one MaskType::Background entry (see
    // RetouchTab::ensureBackgroundMask), so it's already in masks() before
    // any addMask() call — a normal, reorderable, non-pinned entry like any
    // other layer, which is exactly what this test exercises below (it's
    // included in every permutation like the plain layers).
    RetouchTab tab(QSize(8, 8));
    assert(tab.masks().size() == 1);
    assert(tab.masks()[0].type == MaskType::Background);
    tab.addMask(MaskType::Radial);
    tab.addMask(MaskType::Radial);
    tab.addMask(MaskType::Radial);
    // addMask inserts each new layer at index 0 (top of the stack) and
    // selects it, so after three calls masks() is [L2, L1, L0, Background]
    // (insertion order, most-recent first, Background still at the bottom)
    // with L2 active.
    assert(tab.masks().size() == 4);
    assert(tab.activeMaskIndex() == 0);
    assert(tab.masks()[3].type == MaskType::Background);

    // A valid permutation reorders the stack and keeps the active mask
    // correctly tracked to its new position: the active mask was at index 0
    // before the call, and newOrder places old-index-0 at position 1.
    // Background (index 3) is included like any other entry and stays put.
    {
        tab.reorderMasks({2, 0, 1, 3});
        assert(tab.masks().size() == 4);
        assert(tab.activeMaskIndex() == 1);
    }

    // Reset to a known order for the next cases: identity [0, 1, 2, 3].
    tab.reorderMasks({1, 2, 0, 3}); // undoes the previous permutation
    tab.selectMask(1);
    assert(tab.activeMaskIndex() == 1);

    // A malformed order (duplicate index, missing index) must be rejected
    // outright rather than silently dropping/duplicating a mask.
    {
        QVector<Mask> before = tab.masks();
        int activeBefore = tab.activeMaskIndex();
        tab.reorderMasks({0, 0, 2, 3}); // index 1 missing, index 0 duplicated
        assert(tab.masks().size() == before.size());
        for (int i = 0; i < before.size(); ++i)
            assert(tab.masks()[i].type == before[i].type);
        assert(tab.activeMaskIndex() == activeBefore);
    }

    // An out-of-range index must also be rejected.
    {
        QVector<Mask> before = tab.masks();
        tab.reorderMasks({0, 1, 2, 4});
        assert(tab.masks().size() == before.size());
    }

    // Wrong-sized order must be rejected.
    {
        QVector<Mask> before = tab.masks();
        tab.reorderMasks({0, 1});
        assert(tab.masks().size() == before.size());
    }

    // Grouping two layers, then reordering with one of them listed in
    // leftGroupIndices, clears its groupId (mirrors a drag that pulls a
    // layer out of its group) while leaving the group's other member tagged.
    {
        tab.groupMasks({0, 1});
        QString groupId = tab.masks()[0].groupId;
        assert(!groupId.isEmpty());
        assert(tab.masks()[1].groupId == groupId);

        tab.reorderMasks({0, 1, 2, 3}, {0}); // identity order, but layer 0 left its group
        assert(tab.masks()[0].groupId.isEmpty());
        assert(tab.masks()[1].groupId == groupId); // untouched sibling stays grouped

        // Dragging the ungrouped layer 2 into that group (mirrors dropping a
        // row onto a group header/among its members) tags it with the
        // group's id via joinGroups, without disturbing the existing member.
        tab.reorderMasks({0, 1, 2, 3}, {}, {{0, groupId}});
        assert(tab.masks()[0].groupId == groupId);
        assert(tab.masks()[1].groupId == groupId);
    }

    // Regression test: sequential real Shape creation via the actual
    // creation entry point (RetouchTab::addMask(MaskType::Shape, ...), the
    // same call onShapeCreateRequested makes for every canvas drag-to-create
    // gesture) must always insert at true masks index 0, independent of
    // which mask happened to be active/selected beforehand -- mirroring the
    // normal "draw shape, it becomes active, draw the next one" workflow
    // with no deliberate reselection in between.
    {
        RetouchTab tab2(QSize(8, 8));
        assert(tab2.masks().size() == 1); // just Background
        int blueIdx = tab2.addMask(MaskType::Shape, ShapeType::Rectangle);
        tab2.masks()[blueIdx]; // still active after creation (addMask sets m_activeMask)
        int redIdx = tab2.addMask(MaskType::Shape, ShapeType::Ellipse);
        int whiteIdx = tab2.addMask(MaskType::Shape, ShapeType::Rectangle);
        (void)blueIdx; (void)redIdx;
        // Newest-created (white) must be at index 0 (frontmost/top), then
        // red, then blue, then Background last -- true creation-order,
        // newest-is-topmost, matching both the Layers panel (built by
        // walking masks() index 0 upward) and applyMasks' render order.
        assert(tab2.masks().size() == 4);
        assert(whiteIdx == 0);
        assert(tab2.masks()[0].shapeType == ShapeType::Rectangle); // white (created last)
        assert(tab2.masks()[1].shapeType == ShapeType::Ellipse);   // red (created 2nd)
        assert(tab2.masks()[2].shapeType == ShapeType::Rectangle); // blue (created 1st)
        assert(tab2.masks()[3].type == MaskType::Background);
        assert(tab2.activeMaskIndex() == 0); // most recently created shape stays active
    }

    // Regression test: selecting a shape via the canvas (RetouchTab::
    // selectShape, called from onShapeSelected on a canvas click) must keep
    // m_activeMask (the Layers panel's highlighted row, and the target of
    // generic per-mask edits like opacity/blend/visibility) in sync with
    // the shape actually selected -- previously selectShape() only updated
    // m_activeShape, leaving the Layers panel highlighting whatever mask
    // was last active via selectMask()/addMask() and any generic mask edit
    // silently targeting the wrong layer.
    {
        RetouchTab tab3(QSize(8, 8));
        // addMask() always returns 0 (its own just-inserted index at index
        // 0), which goes stale the moment the *next* addMask() shifts it --
        // so blue's real current index after red is created is 1, not the
        // 0 its own addMask() call returned; use the known post-insertion
        // layout instead of the return value for the older shape.
        tab3.addMask(MaskType::Shape, ShapeType::Rectangle); // blue -> ends up at index 1
        int redIdx = tab3.addMask(MaskType::Shape, ShapeType::Ellipse); // red, index 0
        const int blueIdx = 1;
        // selectShape()'s marker-index table (m_shapeMaskIndices) is only
        // (re)built by updateShapeMarkers(), which addMask() alone never
        // calls (only onShapeCreateRequested/selectMask do); an initial
        // selectMask() call here mirrors that and populates the table so
        // selectShape() below has fresh marker indices to resolve against.
        tab3.selectMask(redIdx);
        assert(tab3.activeShapeIndex() == 0); // red's marker index
        // After the two creations above, masks() is [red(0), blue(1), Bg(2)]
        // with red active (most recently created). Clicking blue's shape
        // marker on the canvas (marker index 1, since updateShapeMarkers()
        // walks masks front-to-back skipping Background) must move
        // m_activeMask onto blue's real masks index, not leave it on red.
        assert(tab3.activeMaskIndex() == 0); // red, just created
        tab3.selectShape(1); // marker index 1 -> blue
        assert(tab3.activeShapeIndex() == 1);
        assert(tab3.activeMaskIndex() == blueIdx);

        // And the reverse direction: selecting a mask via the Layers panel
        // (RetouchTab::selectMask, called from LayersPanel::
        // selectMaskRequested) must move the canvas's shape-selection
        // gizmo (m_activeShape) onto it too, not leave canvas selection
        // handles on whatever shape was last clicked on the canvas.
        tab3.selectMask(redIdx);
        assert(tab3.activeMaskIndex() == redIdx);
        assert(tab3.activeShapeIndex() == 0); // red's marker index
    }

    printf("LayerReorderTest passed\n");
    return 0;
}
