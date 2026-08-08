#pragma once

#include <algorithm>

#include <QColor>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "svg/SvgNode.h"

// The document owned/edited by SvgEditorWindow. Nodes are stored flat in
// paint order (later = on top); Group nodes reference children by id via
// SvgNode::childIds rather than nesting containers, keeping iteration and
// serialization simple.
class SvgDocument {
public:
    QSizeF canvasSize{512.0, 512.0};
    QColor backgroundColor = Qt::transparent;
    QVector<SvgNode> nodes;

    int indexOfId(const QString &id) const {
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes[i].id == id) return i;
        return -1;
    }

    SvgNode *findById(const QString &id) {
        int idx = indexOfId(id);
        return idx >= 0 ? &nodes[idx] : nullptr;
    }

    const SvgNode *findById(const QString &id) const {
        int idx = indexOfId(id);
        return idx >= 0 ? &nodes[idx] : nullptr;
    }

    // Appends to the end (top of paint order) and returns its id.
    QString addNode(const SvgNode &node) {
        nodes.append(node);
        return nodes.last().id;
    }

    void removeNode(const QString &id) {
        int idx = indexOfId(id);
        if (idx < 0) return;
        // Detach from any parent group's child list.
        if (!nodes[idx].parentGroupId.isEmpty()) {
            if (SvgNode *parent = findById(nodes[idx].parentGroupId))
                parent->childIds.removeAll(id);
        }
        nodes.removeAt(idx);
    }

    void moveNode(int fromIndex, int toIndex) {
        if (fromIndex < 0 || fromIndex >= nodes.size()) return;
        toIndex = std::clamp(toIndex, 0, int(nodes.size()) - 1);
        nodes.move(fromIndex, toIndex);
    }
};
