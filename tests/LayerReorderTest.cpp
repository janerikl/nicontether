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

    printf("LayerReorderTest passed\n");
    return 0;
}
