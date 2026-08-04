#include "ui/LayersPanel.h"

#include "ui/ColorPanel.h"
#include "ui/DetailEffectsPanel.h"
#include "ui/LevelsPanel.h"
#include "ui/MaskPanel.h"
#include "ui/ToneCurvePanel.h"
#include "ui/TonePanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <cmath>
#include <functional>
#include <QComboBox>
#include <QDockWidget>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPointer>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QCheckBox>
#include <QHash>
#include <QMainWindow>
#include <QMenu>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <cmath>

namespace {
QString maskTypeLabel(MaskType t) {
    switch (t) {
    case MaskType::Radial:  return "Radial";
    case MaskType::Linear:  return "Graduated";
    case MaskType::Brush:   return "Brush";
    case MaskType::Paint:   return "Paint";
    case MaskType::None:    return "Layer";
    case MaskType::Text:    return "Text Mask";
    case MaskType::Shape:   return "Shape";
    case MaskType::TextBox: return "Text";
    }
    return "Layer";
}

QString shapeTypeLabel(ShapeType t) {
    switch (t) {
    case ShapeType::Rectangle: return "Rectangle";
    case ShapeType::Ellipse:   return "Ellipse";
    case ShapeType::Line:      return "Line";
    case ShapeType::Polygon:   return "Polygon";
    case ShapeType::Star:      return "Star";
    case ShapeType::Heart:     return "Heart";
    }
    return "Shape";
}

// Small programmatically-drawn glyphs for the per-section title bar, in the
// same spirit as the tool-bar icons in RetouchWindow.cpp (no image assets).
constexpr int kSectionIconPx = 14;
const QColor kSectionIconColor(190, 190, 190);

QIcon drawChevronIcon(bool expanded) {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kSectionIconColor, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPolygonF tri;
    if (expanded) // down-pointing (content visible)
        tri << QPointF(3, 5) << QPointF(7, 10) << QPointF(11, 5);
    else // right-pointing (collapsed)
        tri << QPointF(5, 2) << QPointF(10, 7) << QPointF(5, 12);
    p.drawPolyline(tri);
    return QIcon(pm);
}

QIcon drawCloseIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kSectionIconColor, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(3, 3), QPointF(11, 11));
    p.drawLine(QPointF(11, 3), QPointF(3, 11));
    return QIcon(pm);
}

// Layer-list action toolbar glyphs (Add/Duplicate/Delete/Group/Ungroup),
// same drawn-not-loaded-asset style and size as the two icons above.
QIcon drawPlusIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kSectionIconColor, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(7, 2), QPointF(7, 12));
    p.drawLine(QPointF(2, 7), QPointF(12, 7));
    return QIcon(pm);
}

QIcon drawDuplicateIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kSectionIconColor, 1.3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(2, 2, 7, 7));
    p.drawRect(QRectF(5, 5, 7, 7));
    return QIcon(pm);
}

QIcon drawTrashIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kSectionIconColor, 1.3);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(3, 4), QPointF(11, 4));
    p.drawLine(QPointF(5, 4), QPointF(5.5, 2));
    p.drawLine(QPointF(9, 4), QPointF(8.5, 2));
    p.drawLine(QPointF(5.5, 2), QPointF(8.5, 2));
    p.drawRect(QRectF(4, 4, 6, 8));
    return QIcon(pm);
}

QIcon drawGroupIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(kSectionIconColor);
    p.drawRoundedRect(QRectF(2, 4, 5, 2.5), 1, 1);
    p.drawRoundedRect(QRectF(2, 5.5, 10, 6.5), 1.5, 1.5);
    return QIcon(pm);
}

QIcon drawUngroupIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(kSectionIconColor);
    p.drawRoundedRect(QRectF(1, 4, 4.5, 2.2), 1, 1);
    p.drawRoundedRect(QRectF(1, 5.4, 8.5, 5.6), 1.3, 1.3);
    p.setBrush(Qt::NoBrush);
    QPen pen(kSectionIconColor, 1.3);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(12, 5), QPointF(12, 11));
    p.drawLine(QPointF(10, 8), QPointF(14, 8));
    return QIcon(pm);
}

// Custom title bar for a per-section dock: a chevron that collapses the
// dock down to just this bar (hides the content widget without closing the
// dock), a title label, and a close button. Replacing QDockWidget's default
// title bar this way means the native close button is gone, so this widget
// draws its own equivalent.
class SectionTitleBar : public QWidget {
public:
    SectionTitleBar(const QString &title, QDockWidget *dock, QWidget *parent = nullptr)
        : QWidget(parent), m_dock(dock) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(2);

        m_chevron = new QToolButton;
        m_chevron->setCheckable(true);
        m_chevron->setChecked(false); // collapsed by default
        m_chevron->setAutoRaise(true);
        m_chevron->setIconSize(QSize(kSectionIconPx, kSectionIconPx));
        connect(m_chevron, &QToolButton::toggled, this, &SectionTitleBar::setExpanded);

        auto *label = new QLabel("<b>" + title + "</b>");

        auto *closeBtn = new QToolButton;
        closeBtn->setAutoRaise(true);
        closeBtn->setIcon(drawCloseIcon());
        closeBtn->setIconSize(QSize(kSectionIconPx, kSectionIconPx));
        closeBtn->setToolTip("Close this section");
        connect(closeBtn, &QToolButton::clicked, m_dock, &QDockWidget::close);

        layout->addWidget(m_chevron);
        layout->addWidget(label, 1);
        layout->addWidget(closeBtn);

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        setExpanded(false); // apply the initial collapsed state to the dock
    }

private:
    // Collapsing hides the content by squashing its height to zero rather
    // than calling setVisible(false) on it: a genuinely hidden widget
    // contributes no width to its dock's sizeHint, which is what was
    // shrinking collapsed rows down to this bar's own minimal width (and
    // dragging the float/close buttons in next to the label). Keeping the
    // content "visible" but zero-height preserves its width contribution —
    // matching the expanded row's width, where this already worked — while
    // still showing nothing.
    void setExpanded(bool expanded) {
        m_chevron->setIcon(drawChevronIcon(expanded));
        if (QWidget *content = m_dock->widget())
            content->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
        if (expanded) {
            m_dock->setMaximumHeight(QWIDGETSIZE_MAX);
        } else {
            m_dock->setMaximumHeight(sizeHint().height());
        }
    }

    QDockWidget *m_dock;
    QToolButton *m_chevron = nullptr;
};

// Blocks internal drag-and-drop from reparenting a row under another row
// (i.e. joining/nesting a group via drag) — grouping only happens through
// the Group/Ungroup buttons. Only "between top-level rows" drops are
// accepted, so a drag can reorder ungrouped layers and whole groups (moved
// as one block) but never nest one inside another.
class MaskTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    // Layers/groups should be freely reorderable, including dragging a
    // layer into a group (dropped on the group's header or squeezed among
    // its members) or out to the top level. The one thing still disallowed
    // is dropping squarely "on" a *plain* top-level row (or Background),
    // which Qt would otherwise interpret as nesting the dragged row as that
    // plain row's child — there's no such thing as a group without a Group
    // header, so that would build a malformed tree.
    void dropEvent(QDropEvent *event) override {
        QModelIndex idx = indexAt(event->position().toPoint());
        if (idx.isValid() && dropIndicatorPosition() == QAbstractItemView::OnItem) {
            QTreeWidgetItem *item = itemFromIndex(idx);
            const bool isGroupHeader = item && item->data(0, Qt::UserRole).toInt() == -1;
            if (!isGroupHeader) {
                event->ignore();
                return;
            }
        }
        QTreeWidget::dropEvent(event);
    }

    // Shift-click range selection is purely visual/row-order, so a range
    // spanning an ungrouped layer above a group and one below it sweeps up
    // the group's parent row (and its expanded members) even though the
    // user only meant to select the ungrouped layers. Unless the group is
    // itself one of the range's endpoints, strip it (and its children) back
    // out of the resulting selection.
    void mousePressEvent(QMouseEvent *event) override {
        if ((event->modifiers() & Qt::ShiftModifier) && !(event->modifiers() & Qt::ControlModifier)) {
            // Read everything we need from the anchor/target items and
            // resolve it to plain data *before* invoking the base handler:
            // QTreeWidget::mousePressEvent synchronously fires
            // currentItemChanged, which cascades up into setMasks() ->
            // rebuildList() -> m_maskList->clear(), deleting every
            // QTreeWidgetItem. Any item pointer captured beforehand (and
            // still referenced afterward) would then be dangling.
            QTreeWidgetItem *anchorItem = currentItem();
            bool anchorIsGroup = anchorItem && anchorItem->data(0, Qt::UserRole).toInt() == -1;
            QTreeWidgetItem *targetItem = itemAt(event->pos());
            bool targetIsGroup = targetItem && targetItem->data(0, Qt::UserRole).toInt() == -1;

            QTreeWidget::mousePressEvent(event);

            if (!anchorIsGroup && !targetIsGroup) {
                for (QTreeWidgetItem *item : selectedItems()) {
                    if (item->data(0, Qt::UserRole).toInt() == -1) {
                        item->setSelected(false);
                        for (int c = 0; c < item->childCount(); ++c)
                            item->child(c)->setSelected(false);
                    }
                }
            }
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }
};
} // namespace

namespace {
constexpr char kCollapsedMaskGroupsKey[] = "layersPanel/collapsedMaskGroups";
}

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent) {
    const QStringList collapsedMasks = QSettings().value(kCollapsedMaskGroupsKey).toStringList();
    m_collapsedMaskGroups = QSet<QString>(collapsedMasks.begin(), collapsedMasks.end());
    // Collapsed section rows have very little intrinsic width (just a
    // chevron + short label); without a floor here the whole panel would
    // shrink to that width whenever every section is collapsed, dragging
    // the float/close buttons in tight against the label instead of sitting
    // out at the right edge.
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Layer list: checkable (visibility) rows, drag to reorder (whole groups
    // move as one block), double-click to rename, Duplicate/Delete/Group.
    // A tree (not a flat list) since groups nest as collapsible parent rows
    // with their members underneath, mirroring the Shapes section below.
    m_maskList = new MaskTreeWidget;
    m_maskList->setHeaderHidden(true);
    m_maskList->setColumnCount(1);
    m_maskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_maskList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_maskList->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    // Photoshop-sized rows: besides looking right, a bigger row also gives
    // MaskTreeWidget::dropEvent's Above/On/Below classification (computed
    // proportionally to row height) much bigger, easier-to-hit drop zones.
    m_maskList->setIconSize(QSize(40, 40));
    // No per-depth indent: a group's members would otherwise render their
    // checkbox/thumbnail shifted right of top-level rows and the group
    // header itself, which is what made the checkbox column look
    // inconsistent. Nesting is still legible from the "Group" header row
    // and its collapse chevron alone.
    m_maskList->setIndentation(0);
    m_maskList->setMinimumHeight(140);
    root->addWidget(m_maskList, 1);

    // Icon toolbar below the list (Photoshop-style), rather than a column of
    // full-width text buttons beside it — frees up the list's full width for
    // bigger, easier-to-hit rows.
    auto *listButtons = new QHBoxLayout;
    auto mkToolButton = [&](const QIcon &icon, const QString &tooltip) {
        auto *b = new QPushButton;
        b->setIcon(icon);
        b->setIconSize(QSize(kSectionIconPx, kSectionIconPx));
        b->setToolTip(tooltip);
        b->setFlat(true);
        b->setFixedSize(28, 28);
        listButtons->addWidget(b);
        return b;
    };
    m_add = mkToolButton(drawPlusIcon(), QStringLiteral("Add Layer"));
    m_duplicate = mkToolButton(drawDuplicateIcon(), QStringLiteral("Duplicate Layer"));
    m_delete = mkToolButton(drawTrashIcon(), QStringLiteral("Delete Layer"));
    m_groupMasks = mkToolButton(drawGroupIcon(), QStringLiteral("Group Layers"));
    m_ungroupMasks = mkToolButton(drawUngroupIcon(), QStringLiteral("Ungroup Layers"));
    listButtons->addStretch(1);
    root->addLayout(listButtons);

    // Name / opacity / blend mode.
    auto *props = new QFormLayout;
    m_name = new QLineEdit;
    props->addRow("Name:", m_name);
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    props->addRow("Opacity:", m_opacity);
    m_blend = new QComboBox;
    m_blend->addItem("Normal", int(BlendMode::Normal));
    m_blend->addItem("Multiply", int(BlendMode::Multiply));
    m_blend->addItem("Screen", int(BlendMode::Screen));
    m_blend->addItem("Overlay", int(BlendMode::Overlay));
    m_blend->addItem("Soft Light", int(BlendMode::SoftLight));
    props->addRow("Blend:", m_blend);
    root->addLayout(props);

    auto *imageHeader = new QLabel("<b>Image Layer</b>");
    root->addWidget(imageHeader);
    auto *imageForm = new QFormLayout;
    auto mkPos = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(-100, 100);
        imageForm->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this] { emitImageTransform(); });
        return s;
    };
    auto mkScale = [&](const QString &label) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(10, 300);
        imageForm->addRow(label + ":", s);
        connect(s, &QSlider::valueChanged, this, [this](int v) {
            if (m_syncing) return;
            if (m_imageLockRatio && m_imageLockRatio->isChecked()) {
                if (this->sender() == m_imageScaleX && m_imageScaleY) {
                    QSignalBlocker b(m_imageScaleY);
                    m_imageScaleY->setValue(v);
                } else if (this->sender() == m_imageScaleY && m_imageScaleX) {
                    QSignalBlocker b(m_imageScaleX);
                    m_imageScaleX->setValue(v);
                }
            }
            emitImageTransform();
        });
        return s;
    };
    m_imagePosX = mkPos("Position X");
    m_imagePosY = mkPos("Position Y");
    m_imageScaleX = mkScale("Scale W");
    m_imageScaleY = mkScale("Scale H");
    m_imageLockRatio = new QCheckBox("Lock ratio");
    imageForm->addRow("", m_imageLockRatio);
    connect(m_imageLockRatio, &QCheckBox::toggled, this, [this](bool on) {
        if (m_syncing) return;
        if (on && m_imageScaleX && m_imageScaleY) {
            QSignalBlocker bx(m_imageScaleX);
            QSignalBlocker by(m_imageScaleY);
            const int v = std::max(m_imageScaleX->value(), m_imageScaleY->value());
            m_imageScaleX->setValue(v);
            m_imageScaleY->setValue(v);
        }
        emitImageTransform();
    });
    root->addLayout(imageForm);

    // Each editing section is its own QDockWidget nested inside a small
    // inner QMainWindow, so it gets a real collapse/float/close title bar
    // while the whole thing still docks/floats as one "Layers" panel.
    m_inner = new QMainWindow;
    m_inner->setWindowFlags(Qt::Widget);
    m_inner->setDockNestingEnabled(true);
    auto *placeholder = new QWidget;
    placeholder->setMaximumHeight(0);
    m_inner->setCentralWidget(placeholder);

    auto addSection = [&](const QString &objName, const QString &title,
                          QWidget *content) {
        auto *dock = new QDockWidget(title);
        dock->setObjectName(objName);
        dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        dock->setWidget(content);
        // Collapsed by default; SectionTitleBar's ctor applies this via
        // setExpanded(false), which squashes content's height rather than
        // hiding it (see SectionTitleBar::setExpanded for why).
        dock->setTitleBarWidget(new SectionTitleBar(title, dock));
        dock->setSizePolicy(QSizePolicy::Expanding, dock->sizePolicy().verticalPolicy());
        m_inner->addDockWidget(Qt::TopDockWidgetArea, dock);
        return dock;
    };

    m_tonePanel = new TonePanel;
    m_toneSectionDock = addSection("layerSectionTone", "Tone", m_tonePanel);
    m_colorPanel = new ColorPanel;
    m_colorSectionDock = addSection("layerSectionColor", "Colour", m_colorPanel);
    m_toneCurvePanel = new ToneCurvePanel;
    m_toneCurveSectionDock = addSection("layerSectionToneCurve", "Tone Curve", m_toneCurvePanel);
    m_levelsPanel = new LevelsPanel;
    // The targeted color-adjustment tool only edits the global (base) levels;
    // hide its toggle in the per-layer panel.
    m_levelsPanel->setTargetPickVisible(false);
    m_levelsSectionDock = addSection("layerSectionLevels", "Levels", m_levelsPanel);
    m_detailEffectsPanel = new DetailEffectsPanel;
    m_detailEffectsSectionDock = addSection("layerSectionDetailEffects", "Detail & Effects",
                                            m_detailEffectsPanel);
    m_maskPanel = new MaskPanel;
    m_masksSectionDock = addSection("layerSectionMasks", "Masks", m_maskPanel);

    // Remove Object section: a flat checkable list (no grouping/reordering
    // needed), one row per cached content-aware fill, plus a Delete button —
    // mirrors the masks list's eye-toggle + Delete pattern above.
    auto *removalsContent = new QWidget;
    auto *removalsLayout = new QVBoxLayout(removalsContent);
    removalsLayout->setContentsMargins(4, 4, 4, 4);
    m_removalList = new QListWidget;
    m_removalList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_removalList->setMinimumHeight(100);
    removalsLayout->addWidget(m_removalList, 1);
    auto *removalButtons = new QHBoxLayout;
    m_deleteRemoval = new QPushButton("Delete");
    removalButtons->addWidget(m_deleteRemoval);
    removalButtons->addStretch(1);
    removalsLayout->addLayout(removalButtons);
    m_removalsSectionDock = addSection("layerSectionRemovals", "Remove Object", removalsContent);

    // Stack the eight sections top-to-bottom.
    m_inner->splitDockWidget(m_toneSectionDock, m_colorSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_colorSectionDock, m_toneCurveSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_toneCurveSectionDock, m_levelsSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_levelsSectionDock, m_detailEffectsSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_detailEffectsSectionDock, m_masksSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_masksSectionDock, m_removalsSectionDock, Qt::Vertical);

    root->addWidget(m_inner, 2);

    auto *addMenu = new QMenu(m_add);
    QAction *addPaintAction = addMenu->addAction("Paint");
    connect(addPaintAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(MaskType::Paint); });
    QAction *addRadialAction = addMenu->addAction("Radial");
    connect(addRadialAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(MaskType::Radial); });
    QAction *addLinearAction = addMenu->addAction("Linear");
    connect(addLinearAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(MaskType::Linear); });
    QAction *addLayerAction = addMenu->addAction("Adjustment Layer");
    connect(addLayerAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(MaskType::None); });

    QMenu *shapeMenu = addMenu->addMenu("Shape");
    auto addShapeAction = [&](const QString &label, ShapeType type) {
        QAction *act = shapeMenu->addAction(label);
        connect(act, &QAction::triggered, this,
                [this, type] { emit addLayerRequested(MaskType::Shape, type); });
    };
    addShapeAction("Rectangle", ShapeType::Rectangle);
    addShapeAction("Ellipse", ShapeType::Ellipse);
    addShapeAction("Line", ShapeType::Line);
    addShapeAction("Polygon", ShapeType::Polygon);
    addShapeAction("Star", ShapeType::Star);
    addShapeAction("Heart", ShapeType::Heart);

    QAction *addTextBoxAction = addMenu->addAction("Text Box");
    connect(addTextBoxAction, &QAction::triggered, this,
            [this] { emit addLayerRequested(MaskType::TextBox); });

    QAction *addImageAction = addMenu->addAction("Add Image Layer…");
    connect(addImageAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(
            this, "Add Image Layer", QString(),
            "Images (*.jpg *.jpeg *.png *.tif *.tiff *.nef *.NEF);;All Files (*)");
        if (!path.isEmpty()) emit addImageLayerRequested(path);
    });
    m_add->setMenu(addMenu);

    connect(m_duplicate, &QPushButton::clicked, this,
            [this] { emit duplicateMaskRequested(); });
    connect(m_delete, &QPushButton::clicked, this,
            [this] { emit deleteMaskRequested(); });
    connect(m_maskList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (m_syncing || !item) return;
                int idx = item->data(0, Qt::UserRole).toInt();
                // A group's parent row has no mask of its own; select its
                // first child instead.
                if (idx == -1 && item->childCount() > 0)
                    idx = item->child(0)->data(0, Qt::UserRole).toInt();
                emit selectMaskRequested(idx == -2 ? -1 : idx); // -2 = Background row
            });
    connect(m_maskList, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int) {
                if (m_syncing) return;
                int idx = item->data(0, Qt::UserRole).toInt();
                if (idx == -1) return; // group parent row: no checkbox/name of its own
                const bool checked = item->checkState(0) == Qt::Checked;
                const bool wasChecked = item->data(0, Qt::UserRole + 2).toBool();
                if (checked != wasChecked) {
                    item->setData(0, Qt::UserRole + 2, checked);
                    const int emitIdx = idx == -2 ? -1 : idx; // -2 = Background row
                    QPointer<LayersPanel> self(this);
                    QMetaObject::invokeMethod(
                        this,
                        [self, emitIdx, checked] {
                            if (self) emit self->maskVisibleChanged(emitIdx, checked);
                        },
                        Qt::QueuedConnection);
                    return;
                }
                if (idx >= 0) emit maskRenamed(idx, item->text(0));
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                if (m_syncing) return;
                // Flatten the tree back into a full masks() order: a group's
                // parent row expands to its members' original indices, a
                // plain row is its own index. Background (idx == -2) isn't
                // part of masks() and is skipped.
                QVector<int> order;
                QVector<int> leftGroup; // original indices no longer nested under a group
                QVector<QPair<int, QString>> joinGroup; // original indices that joined/switched group
                for (int i = 0; i < m_maskList->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *item = m_maskList->topLevelItem(i);
                    int idx = item->data(0, Qt::UserRole).toInt();
                    if (idx == -2) continue; // Background
                    if (idx == -1) { // group parent: append its members in order
                        const QString groupId = item->data(0, Qt::UserRole + 1).toString();
                        for (int c = 0; c < item->childCount(); ++c) {
                            int childIdx = item->child(c)->data(0, Qt::UserRole).toInt();
                            order.append(childIdx);
                            if (childIdx >= 0 && childIdx < m_masks.size() &&
                                m_masks[childIdx].groupId != groupId)
                                joinGroup.append({childIdx, groupId}); // dropped into this group
                        }
                        continue;
                    }
                    order.append(idx);
                    if (idx >= 0 && idx < m_masks.size() && !m_masks[idx].groupId.isEmpty())
                        leftGroup.append(idx); // was grouped, now a top-level row
                }
                if (order.size() != m_masks.size()) return;
                // Deferred: rowsMoved fires synchronously from inside
                // QTreeWidget::dropEvent, while Qt's internal drag-drop
                // machinery is still unwinding on top of the tree's items.
                // Emitting synchronously here cascades into setMasks() ->
                // rebuildList() -> m_maskList->clear(), deleting those items
                // out from under the in-progress dropEvent call and crashing.
                QPointer<LayersPanel> self(this);
                QMetaObject::invokeMethod(
                    this,
                    [self, order, leftGroup, joinGroup] {
                        if (self) emit self->maskReorderRequested(order, leftGroup, joinGroup);
                    },
                    Qt::QueuedConnection);
            });
    connect(m_maskList, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (m_syncing) return;
        QString groupId = item->data(0, Qt::UserRole + 1).toString();
        if (groupId.isEmpty()) return;
        m_collapsedMaskGroups.insert(groupId);
        QSettings().setValue(kCollapsedMaskGroupsKey,
                             QStringList(m_collapsedMaskGroups.begin(), m_collapsedMaskGroups.end()));
    });
    connect(m_maskList, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (m_syncing) return;
        QString groupId = item->data(0, Qt::UserRole + 1).toString();
        if (groupId.isEmpty()) return;
        m_collapsedMaskGroups.remove(groupId);
        QSettings().setValue(kCollapsedMaskGroupsKey,
                             QStringList(m_collapsedMaskGroups.begin(), m_collapsedMaskGroups.end()));
    });
    connect(m_groupMasks, &QPushButton::clicked, this, [this] {
        QVector<int> indices;
        for (QTreeWidgetItem *item : m_maskList->selectedItems()) {
            int idx = item->data(0, Qt::UserRole).toInt();
            if (idx >= 0) indices.append(idx);
        }
        if (indices.size() >= 2) emit groupMasksRequested(indices);
    });
    connect(m_ungroupMasks, &QPushButton::clicked, this, [this] {
        QVector<int> indices;
        for (QTreeWidgetItem *item : m_maskList->selectedItems()) {
            int idx = item->data(0, Qt::UserRole).toInt();
            if (idx >= 0) indices.append(idx);
        }
        if (!indices.isEmpty()) emit ungroupMasksRequested(indices);
    });
    connect(m_name, &QLineEdit::editingFinished, this,
            [this] { if (!m_syncing) emit maskNameChanged(m_name->text()); });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing) emit maskOpacityChanged(v / 100.0);
    });
    connect(m_blend, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_syncing) return;
                emit maskBlendChanged(BlendMode(m_blend->currentData().toInt()));
            });

    connect(m_tonePanel, &TonePanel::adjustChanged, this,
            [this](int brightness, int contrast, int highlights, int shadows) {
                m_curAdjust.brightness = brightness;
                m_curAdjust.contrast = contrast;
                m_curAdjust.highlights = highlights;
                m_curAdjust.shadows = shadows;
                emitAdjust();
            });
    connect(m_colorPanel, &ColorPanel::adjustChanged, this,
            [this](int saturation, int vibrance, int temperature, int tint) {
                m_curAdjust.saturation = saturation;
                m_curAdjust.vibrance = vibrance;
                m_curAdjust.temperature = temperature;
                m_curAdjust.tint = tint;
                emitAdjust();
            });
    connect(m_toneCurvePanel, &ToneCurvePanel::curveChanged, this,
            [this](const QVector<QPointF> &curve) {
                m_curAdjust.curve = curve;
                emitAdjust();
            });
    connect(m_levelsPanel, &LevelsPanel::levelsChanged, this,
            [this](const Levels &lv) {
                m_curAdjust.levels = lv;
                emitAdjust();
            });
    connect(m_detailEffectsPanel, &DetailEffectsPanel::adjustChanged, this,
            [this](int clarity, int sharpen, int vignette, int lightAngle,
                   int lightIntensity) {
                m_curAdjust.clarity = clarity;
                m_curAdjust.sharpen = sharpen;
                m_curAdjust.vignette = vignette;
                m_curAdjust.lightAngle = lightAngle;
                m_curAdjust.lightIntensity = lightIntensity;
                emitAdjust();
            });
    connect(m_maskPanel, &MaskPanel::maskTypeChanged, this, &LayersPanel::maskTypeChanged);
    connect(m_maskPanel, &MaskPanel::maskShapeChanged, this, &LayersPanel::maskShapeChanged);
    connect(m_maskPanel, &MaskPanel::maskTextChanged, this, &LayersPanel::maskTextChanged);

    connect(m_removalList, &QListWidget::currentRowChanged, this, [this](int uiRow) {
        if (m_syncing) return;
        int idx = (uiRow >= 0 && uiRow < m_removals.size()) ? m_removals.size() - 1 - uiRow : -1;
        emit selectRemovalRequested(idx);
    });
    connect(m_removalList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_syncing) return;
        int uiRow = m_removalList->row(item);
        int idx = m_removals.size() - 1 - uiRow;
        if (idx >= 0 && idx < m_removals.size())
            emit removalVisibleChanged(idx, item->checkState() == Qt::Checked);
    });
    connect(m_deleteRemoval, &QPushButton::clicked, this, [this] {
        int uiRow = m_removalList->currentRow();
        int idx = (uiRow >= 0 && uiRow < m_removals.size()) ? m_removals.size() - 1 - uiRow : -1;
        if (idx >= 0) emit deleteRemovalRequested(idx);
    });

    clear();
}

void LayersPanel::clear() {
    m_masks.clear();
    m_active = -1;
    m_removals.clear();
    m_activeRemoval = -1;
    m_syncing = true;
    m_maskList->clear();
    m_removalList->clear();
    m_imagePosX->setValue(0);
    m_imagePosY->setValue(0);
    m_imageScaleX->setValue(100);
    m_imageScaleY->setValue(100);
    m_imagePosX->setEnabled(false);
    m_imagePosY->setEnabled(false);
    m_imageScaleX->setEnabled(false);
    m_imageScaleY->setEnabled(false);
    m_imageLockRatio->setEnabled(false);
    m_syncing = false;
    setEnabled(false);
}

void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex, bool hasBackground,
                            bool backgroundHidden, const QImage &previewImage) {
    // A plain click to select a row round-trips through RetouchTab::selectMask
    // -> masksChanged -> here with the mask list content completely unchanged
    // (only activeIndex differs). Rebuilding the tree in that case destroys
    // and recreates every QTreeWidgetItem, which invalidates the
    // QPersistentModelIndex Qt just recorded for the row the user pressed
    // down on -- silently defeating drag-to-reorder on every single click,
    // since the pressed row no longer exists by the time the mouse moves far
    // enough to cross the drag-start threshold. So: only rebuild when the
    // content actually changed; otherwise just move the highlighted row.
    const bool sameContent = masksContentEqual(masks, hasBackground, backgroundHidden);
    m_masks = masks;
    m_active = activeIndex;
    m_hasBackground = hasBackground;
    m_backgroundHidden = backgroundHidden;
    // Stored opportunistically, independent of sameContent: the preview
    // image changes on essentially every render, and forcing a tree rebuild
    // whenever it does would defeat the whole point of masksContentEqual()
    // above. It's simply picked up next time a real (structural) rebuild
    // happens to run.
    if (!previewImage.isNull()) m_backgroundPreview = previewImage;
    setEnabled(true);
    if (sameContent) updateCurrentItemHighlight();
    else rebuildList();
    loadActive();
}

void LayersPanel::setRemovals(const QVector<RemoveObjectOp> &removals, int activeIndex) {
    m_removals = removals;
    m_activeRemoval = activeIndex;
    setEnabled(true);
    rebuildRemovalList();
}

void LayersPanel::setLevelsPreviewImage(const QImage &img) {
    if (m_active >= 0 && m_active < m_masks.size()) m_levelsPanel->setImage(img);
}

void LayersPanel::setMaskBrushRadius(double radiusNorm) {
    if (m_maskPanel) m_maskPanel->setBrushRadius(radiusNorm);
}

QVector<QDockWidget *> LayersPanel::sectionDocks() const {
    return {m_toneSectionDock, m_colorSectionDock, m_toneCurveSectionDock,
            m_levelsSectionDock, m_detailEffectsSectionDock, m_masksSectionDock,
            m_removalsSectionDock};
}

QByteArray LayersPanel::innerDockState() const {
    return m_inner->saveState();
}

void LayersPanel::restoreInnerDockState(const QByteArray &state) {
    m_inner->restoreState(state);
}

void LayersPanel::resetSections() {
    for (QDockWidget *d : sectionDocks())
        if (d) d->show();
}

// True when `masks`/hasBackground/backgroundHidden describe exactly the same
// layer stack already shown in the tree (m_masks et al) — the only thing
// that may differ is which one is active/selected. Mask::operator== covers
// every layer-identity/content field except sourceImageCache and
// sourceMissing (both transient decode results, deliberately excluded from
namespace {
constexpr int kThumbPx = 40;
const QColor kThumbBg(46, 46, 46);
const QColor kThumbFg(225, 225, 225);
}

// Row thumbnails, Photoshop-style: image layers show their actual scaled
// content (see maskThumbnail()); geometry-based masks (radial/linear/brush/
// paint/text/none) have no standalone pixel content of their own, so they
// get a small schematic icon of their geometry instead of a full composite —
// applyMasks()'s rasterizers (Adjustments.cpp) are tuned for full-image
// coverage buffers, not icon-sized previews, and aren't exposed for reuse
// here. Same drawn-not-loaded-asset style as drawChevronIcon/drawCloseIcon
// above.
QIcon LayersPanel::maskThumbnail(const Mask &m) const {
    if (m.isImageLayer()) {
        if (!m.sourceImageCache.isNull()) {
            QPixmap pm = QPixmap::fromImage(m.sourceImageCache)
                             .scaled(kThumbPx, kThumbPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap canvas(kThumbPx, kThumbPx);
            canvas.fill(kThumbBg);
            QPainter p(&canvas);
            p.drawPixmap((kThumbPx - pm.width()) / 2, (kThumbPx - pm.height()) / 2, pm);
            return QIcon(canvas);
        }
        // Loading or missing source: fall through to a neutral placeholder.
        QPixmap pm(kThumbPx, kThumbPx);
        pm.fill(kThumbBg);
        return QIcon(pm);
    }

    QPixmap pm(kThumbPx, kThumbPx);
    pm.fill(kThumbBg);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    switch (m.type) {
    case MaskType::Radial: {
        QPen pen(kThumbFg, 2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QPointF c(m.center.x() * kThumbPx, m.center.y() * kThumbPx);
        p.drawEllipse(c, m.radiusX * kThumbPx, m.radiusY * kThumbPx);
        break;
    }
    case MaskType::Linear: {
        QLinearGradient grad(m.p0.x() * kThumbPx, m.p0.y() * kThumbPx,
                              m.p1.x() * kThumbPx, m.p1.y() * kThumbPx);
        grad.setColorAt(0, kThumbFg);
        grad.setColorAt(1, kThumbBg);
        p.fillRect(pm.rect(), grad);
        break;
    }
    case MaskType::Brush:
    case MaskType::Paint: {
        QPen pen(m.type == MaskType::Paint ? m.paintColor : kThumbFg, 2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        QPainterPath path;
        bool first = true;
        for (const BrushStrokePoint &pt : m.stroke) {
            const QPointF sp(pt.pt.x() * kThumbPx, pt.pt.y() * kThumbPx);
            if (first) { path.moveTo(sp); first = false; }
            else path.lineTo(sp);
        }
        p.drawPath(path);
        break;
    }
    case MaskType::Text: {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(kThumbPx * 2 / 3);
        p.setFont(f);
        p.setPen(kThumbFg);
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("T"));
        break;
    }
    case MaskType::None:
        break;
    case MaskType::Shape: {
        // Simple schematic outline of the shape's kind, tinted with its own
        // fill/stroke colors so the row gives some hint of the actual layer
        // without needing the full ShapeTool rasterizer.
        QColor fill = m.shapeFillEnabled ? m.shapeFillColor : Qt::transparent;
        QColor stroke = m.shapeStrokeEnabled ? m.shapeStrokeColor : kThumbFg;
        p.setBrush(fill.alpha() > 0 ? fill : Qt::NoBrush);
        p.setPen(QPen(stroke, 2));
        const QRectF r(kThumbPx * 0.18, kThumbPx * 0.18, kThumbPx * 0.64, kThumbPx * 0.64);
        switch (m.shapeType) {
        case ShapeType::Rectangle:
            p.drawRect(r);
            break;
        case ShapeType::Ellipse:
            p.drawEllipse(r);
            break;
        case ShapeType::Line:
            p.drawLine(r.topLeft(), r.bottomRight());
            break;
        case ShapeType::Polygon:
        case ShapeType::Star: {
            const int sides = qMax(3, m.shapeSides);
            const QPointF c = r.center();
            const double radius = r.width() / 2.0;
            const double innerRadius = m.shapeType == ShapeType::Star
                                            ? radius * m.shapeInnerRadiusRatio
                                            : radius;
            const int pts = m.shapeType == ShapeType::Star ? sides * 2 : sides;
            QPolygonF poly;
            for (int i = 0; i < pts; ++i) {
                const double a = -M_PI / 2 + i * M_PI / (pts / 2.0);
                const double rad = (m.shapeType == ShapeType::Star && i % 2) ? innerRadius : radius;
                poly << QPointF(c.x() + rad * std::cos(a), c.y() + rad * std::sin(a));
            }
            p.drawPolygon(poly);
            break;
        }
        case ShapeType::Heart: {
            QPainterPath path;
            const double w = r.width(), h = r.height();
            path.moveTo(r.left() + w / 2, r.top() + h * 0.28);
            path.cubicTo(r.left() - w * 0.1, r.top() - h * 0.1, r.left() + w * 0.15,
                         r.top() + h * 0.55, r.left() + w / 2, r.bottom());
            path.cubicTo(r.left() + w * 0.85, r.top() + h * 0.55, r.right() + w * 0.1,
                         r.top() - h * 0.1, r.left() + w / 2, r.top() + h * 0.28);
            p.drawPath(path);
            break;
        }
        }
        break;
    }
    case MaskType::TextBox: {
        // A rendered-content preview: the layer's own text color, echoing
        // how image layers show real pixel content instead of a schematic.
        QFont f = p.font();
        f.setBold(m.textBoxBold);
        f.setItalic(m.textBoxItalic);
        f.setPixelSize(kThumbPx / 2);
        p.setFont(f);
        p.setPen(m.textBoxColor.alpha() > 0 ? m.textBoxColor : kThumbFg);
        const QString sample = m.textBoxText.isEmpty() ? QStringLiteral("T") : m.textBoxText.left(3);
        p.drawText(pm.rect(), Qt::AlignCenter, sample);
        break;
    }
    }
    return QIcon(pm);
}

// Simple folder glyph for a group's parent row.
QIcon LayersPanel::groupThumbnail() const {
    QPixmap pm(kThumbPx, kThumbPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF body(kThumbPx * 0.12, kThumbPx * 0.3, kThumbPx * 0.76, kThumbPx * 0.5);
    QRectF tab(kThumbPx * 0.12, kThumbPx * 0.2, kThumbPx * 0.32, kThumbPx * 0.14);
    p.setPen(Qt::NoPen);
    p.setBrush(kThumbFg.darker(115));
    p.drawRoundedRect(tab, 2, 2);
    p.drawRoundedRect(body, 3, 3);
    return QIcon(pm);
}

// Pinned Background row: a scaled copy of the tab's current composited
// render (see setMasks()'s previewImage parameter), same scale-to-fit
// treatment as an image layer's own thumbnail.
QIcon LayersPanel::backgroundThumbnail() const {
    if (m_backgroundPreview.isNull()) {
        QPixmap pm(kThumbPx, kThumbPx);
        pm.fill(kThumbBg);
        return QIcon(pm);
    }
    QPixmap pm = QPixmap::fromImage(m_backgroundPreview)
                     .scaled(kThumbPx, kThumbPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap canvas(kThumbPx, kThumbPx);
    canvas.fill(kThumbBg);
    QPainter p(&canvas);
    p.drawPixmap((kThumbPx - pm.width()) / 2, (kThumbPx - pm.height()) / 2, pm);
    return QIcon(canvas);
}

// operator== elsewhere), so those two are compared separately here since
// they affect the tree row's " (loading…)"/" (missing)" label text.
bool LayersPanel::masksContentEqual(const QVector<Mask> &masks, bool hasBackground,
                                     bool backgroundHidden) const {
    if (masks.size() != m_masks.size()) return false;
    if (hasBackground != m_hasBackground || backgroundHidden != m_backgroundHidden) return false;
    if (masks != m_masks) return false;
    for (int i = 0; i < masks.size(); ++i) {
        if (masks[i].sourceImageCache.isNull() != m_masks[i].sourceImageCache.isNull()) return false;
        if (masks[i].sourceMissing != m_masks[i].sourceMissing) return false;
    }
    return true;
}

// Builds the tree top-to-bottom to match the stack's top-to-bottom render
// order: walks m_masks from the highest index down, creating a "Group"
// parent row the first time each groupId is seen and nesting every
// subsequent same-group layer under it.
// Rebuilding clear()s and recreates every QTreeWidgetItem in m_maskList,
// which is unsafe to do synchronously from within a call chain Qt's own
// drag-and-drop machinery is still unwinding on top of (e.g. rowsMoved fired
// from dropEvent, or an unrelated async signal landing mid-drag) — it would
// delete items DnD internals still hold pointers to. Rather than try to
// track exactly when a drag is/isn't safely finished (fragile: any missed
// transition leaves rebuilds silently stuck off forever), every call is
// unconditionally coalesced onto the next event-loop turn, by which point
// any in-progress dropEvent/exec() call frame has already unwound.
void LayersPanel::rebuildList() {
    if (m_rebuildScheduled) return;
    m_rebuildScheduled = true;
    QPointer<LayersPanel> self(this);
    QMetaObject::invokeMethod(
        this, [self] { if (self) self->doRebuildList(); }, Qt::QueuedConnection);
}

void LayersPanel::doRebuildList() {
    m_rebuildScheduled = false;
    m_syncing = true;
    QSet<int> selectedIndices;
    for (QTreeWidgetItem *item : m_maskList->selectedItems())
        selectedIndices.insert(item->data(0, Qt::UserRole).toInt());
    m_maskList->clear();
    QHash<QString, QTreeWidgetItem *> groupItems;
    for (int i = m_masks.size() - 1; i >= 0; --i) {
        const Mask &m = m_masks[i];
        QTreeWidgetItem *parent = nullptr;
        if (!m.groupId.isEmpty()) {
            auto it = groupItems.constFind(m.groupId);
            if (it == groupItems.constEnd()) {
                auto *g = new QTreeWidgetItem(m_maskList, {QStringLiteral("Group")});
                g->setIcon(0, groupThumbnail());
                g->setData(0, Qt::UserRole, -1);
                g->setData(0, Qt::UserRole + 1, m.groupId);
                groupItems.insert(m.groupId, g);
                parent = g;
            } else {
                parent = it.value();
            }
        }
        QString label;
        if (!m.name.isEmpty()) {
            label = m.name;
        } else if (m.type == MaskType::Shape) {
            // Shape masks created via a later-session canvas tool get an
            // auto-generated name; ones created via the Add Layer menu
            // (this session) don't yet, so fall back to the shape kind.
            label = shapeTypeLabel(m.shapeType);
        } else if (m.type == MaskType::TextBox) {
            label = QStringLiteral("Text");
        } else {
            label = QString("Layer %1 (%2)").arg(i + 1).arg(maskTypeLabel(m.type));
        }
        if (m.isImageLayer()) {
            if (m.sourceMissing) label += " (missing)";
            else if (m.sourceImageCache.isNull()) label += " (loading…)";
        }
        auto *item = parent ? new QTreeWidgetItem(parent, {label})
                             : new QTreeWidgetItem(m_maskList, {label});
        item->setIcon(0, maskThumbnail(m));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, m.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, i);
        item->setData(0, Qt::UserRole + 2, m.visible);
        if (i == m_active) m_maskList->setCurrentItem(item);
        if (selectedIndices.contains(i)) item->setSelected(true);
    }
    for (auto it = groupItems.constBegin(); it != groupItems.constEnd(); ++it)
        it.value()->setExpanded(!m_collapsedMaskGroups.contains(it.key()));
    if (m_hasBackground) {
        // Pinned to the bottom of the stack, like Photoshop's Background layer.
        // Unlike other rows it can't be dragged or grouped, but it can be
        // hidden (eye checkbox) and deleted (see loadActive()'s isBackground
        // handling).
        auto *bg = new QTreeWidgetItem(m_maskList, {QStringLiteral("Background \xF0\x9F\x94\x92")}); // trailing lock emoji
        bg->setIcon(0, backgroundThumbnail());
        bg->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        bg->setData(0, Qt::UserRole, -2);
        bg->setCheckState(0, m_backgroundHidden ? Qt::Unchecked : Qt::Checked);
        bg->setData(0, Qt::UserRole + 2, !m_backgroundHidden);
        if (m_active == -1) m_maskList->setCurrentItem(bg);
        if (selectedIndices.contains(-2)) bg->setSelected(true);
    }
    m_syncing = false;
}

// Non-destructive counterpart to doRebuildList(): moves the tree's current
// item to match m_active without touching any QTreeWidgetItem, so it's safe
// to call from a plain-selection setMasks() (see its comment) without
// disturbing Qt's own in-flight drag/press bookkeeping. Group members are
// nested, so search every level via QTreeWidgetItemIterator.
void LayersPanel::updateCurrentItemHighlight() {
    const int wantRole = (m_active == -1 && m_hasBackground) ? -2 : m_active;
    for (QTreeWidgetItemIterator it(m_maskList); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == wantRole) {
            m_syncing = true;
            m_maskList->setCurrentItem(*it);
            m_syncing = false;
            return;
        }
    }
}

// Flat list, most-recent removal first (top of stack), same eye-toggle
// pattern as the masks list; each row's UI position maps to
// m_removals.size() - 1 - uiRow to keep index 0 (oldest) at the bottom.
void LayersPanel::rebuildRemovalList() {
    m_syncing = true;
    m_removalList->clear();
    for (int i = m_removals.size() - 1; i >= 0; --i) {
        const RemoveObjectOp &r = m_removals[i];
        auto *item = new QListWidgetItem(QString("Object Removal %1").arg(i + 1));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(r.visible ? Qt::Checked : Qt::Unchecked);
        m_removalList->addItem(item);
        if (i == m_activeRemoval) m_removalList->setCurrentItem(item);
    }
    m_deleteRemoval->setEnabled(m_activeRemoval >= 0 && m_activeRemoval < m_removals.size());
    m_syncing = false;
}

void LayersPanel::loadActive() {
    const bool has = m_active >= 0 && m_active < m_masks.size();
    const bool isBackground = m_hasBackground && m_active == -1;
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_levelsPanel})
        w->setEnabled(has);
    m_duplicate->setEnabled(has || isBackground);
    m_delete->setEnabled(has || isBackground); // background delete handled specially, see deleteMaskRequested
    if (has) {
        const Mask &m = m_masks[m_active];
        m_name->setText(m.name);
        m_opacity->setValue(int(std::lround(m.opacity * 100)));
        int blendIdx = m_blend->findData(int(m.blend));
        m_blend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);
        m_curAdjust = m.adj;
        m_tonePanel->setAdjustments(m_curAdjust.brightness, m_curAdjust.contrast,
                                     m_curAdjust.highlights, m_curAdjust.shadows);
        m_colorPanel->setAdjustments(m_curAdjust.saturation, m_curAdjust.vibrance,
                                      m_curAdjust.temperature, m_curAdjust.tint);
        m_toneCurvePanel->setCurve(m_curAdjust.curve);
        m_levelsPanel->setLevels(m_curAdjust.levels);
        m_detailEffectsPanel->setAdjustments(m_curAdjust.clarity, m_curAdjust.sharpen,
                                              m_curAdjust.vignette, m_curAdjust.lightAngle,
                                              m_curAdjust.lightIntensity);
        m_maskPanel->setMask(m, true);
        const bool imageLayer = m.isImageLayer();
        m_imagePosX->setEnabled(imageLayer);
        m_imagePosY->setEnabled(imageLayer);
        m_imageScaleX->setEnabled(imageLayer);
        m_imageScaleY->setEnabled(imageLayer);
        m_imageLockRatio->setEnabled(imageLayer);
        m_imagePosX->setValue(imageLayer ? int(std::lround(m.sourceImageOffset.x() * 100.0)) : 0);
        m_imagePosY->setValue(imageLayer ? int(std::lround(m.sourceImageOffset.y() * 100.0)) : 0);
        m_imageScaleX->setValue(imageLayer ? int(std::lround(m.sourceImageScale.x() * 100.0)) : 100);
        m_imageScaleY->setValue(imageLayer ? int(std::lround(m.sourceImageScale.y() * 100.0)) : 100);
        m_imageLockRatio->setChecked(imageLayer ? m.sourceImageLockRatio : true);
    } else {
        m_curAdjust = MaskAdjust();
        m_tonePanel->clear();
        m_colorPanel->clear();
        m_toneCurvePanel->clear();
        m_levelsPanel->clear();
        m_detailEffectsPanel->clear();
        m_maskPanel->clear();
        m_imagePosX->setEnabled(false);
        m_imagePosY->setEnabled(false);
        m_imageScaleX->setEnabled(false);
        m_imageScaleY->setEnabled(false);
        m_imageLockRatio->setEnabled(false);
        m_imagePosX->setValue(0);
        m_imagePosY->setValue(0);
        m_imageScaleX->setValue(100);
        m_imageScaleY->setValue(100);
    }
    m_syncing = false;
}

void LayersPanel::emitAdjust() {
    if (m_syncing) return;
    emit maskAdjustChanged(m_curAdjust);
}

void LayersPanel::emitImageTransform() {
    if (m_syncing) return;
    if (m_active < 0 || m_active >= m_masks.size() || !m_masks[m_active].isImageLayer())
        return;
    emit maskImageTransformChanged(m_imagePosX->value() / 100.0,
                                   m_imagePosY->value() / 100.0,
                                   m_imageScaleX->value() / 100.0,
                                   m_imageScaleY->value() / 100.0,
                                   m_imageLockRatio->isChecked());
}
