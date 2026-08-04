#include "ui/LayersPanel.h"

#include "ui/ColorPanel.h"
#include "ui/DetailEffectsPanel.h"
#include "ui/LevelsPanel.h"
#include "ui/MaskPanel.h"
#include "ui/ToneCurvePanel.h"
#include "ui/TonePanel.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QCheckBox>
#include <QHash>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <cmath>

namespace {
QString maskTypeLabel(MaskType t) {
    switch (t) {
    case MaskType::Radial: return "Radial";
    case MaskType::Linear: return "Graduated";
    case MaskType::Brush:  return "Brush";
    case MaskType::Paint:  return "Paint";
    case MaskType::None:   return "Layer";
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
} // namespace

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent) {
    // Collapsed section rows have very little intrinsic width (just a
    // chevron + short label); without a floor here the whole panel would
    // shrink to that width whenever every section is collapsed, dragging
    // the float/close buttons in tight against the label instead of sitting
    // out at the right edge.
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Layer list: checkable (visibility) rows, drag to reorder, Duplicate/Delete.
    auto *selRow = new QHBoxLayout;
    m_maskList = new QListWidget;
    m_maskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_maskList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_maskList->setMinimumHeight(140);
    selRow->addWidget(m_maskList, 1);
    auto *listButtons = new QVBoxLayout;
    m_add = new QPushButton("Add");
    m_duplicate = new QPushButton("Duplicate");
    m_delete = new QPushButton("Delete");
    listButtons->addWidget(m_add);
    listButtons->addWidget(m_duplicate);
    listButtons->addWidget(m_delete);
    listButtons->addStretch(1);
    selRow->addLayout(listButtons);
    root->addLayout(selRow, 1);

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

    // Shapes section: a tree (not a list, unlike the mask stack above) since
    // groups nest as collapsible parent rows with their members underneath.
    auto *shapesContent = new QWidget;
    auto *shapesLayout = new QVBoxLayout(shapesContent);
    shapesLayout->setContentsMargins(4, 4, 4, 4);
    m_shapeList = new QTreeWidget;
    m_shapeList->setHeaderHidden(true);
    m_shapeList->setColumnCount(1);
    m_shapeList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_shapeList->setMinimumHeight(120);
    shapesLayout->addWidget(m_shapeList, 1);
    auto *shapeButtons = new QHBoxLayout;
    m_groupShapes = new QPushButton("Group");
    m_ungroupShapes = new QPushButton("Ungroup");
    shapeButtons->addWidget(m_groupShapes);
    shapeButtons->addWidget(m_ungroupShapes);
    shapeButtons->addStretch(1);
    shapesLayout->addLayout(shapeButtons);
    m_shapesSectionDock = addSection("layerSectionShapes", "Shapes", shapesContent);

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
    m_inner->splitDockWidget(m_masksSectionDock, m_shapesSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_shapesSectionDock, m_removalsSectionDock, Qt::Vertical);

    root->addWidget(m_inner, 2);

    auto *addMenu = new QMenu(m_add);
    QAction *addLayerAction = addMenu->addAction("Add Layer");
    connect(addLayerAction, &QAction::triggered, this,
            [this] { emit addMaskRequested(); });
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
    connect(m_maskList, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!m_syncing)
            emit selectMaskRequested(m_hasBackground && i == m_masks.size() ? -1 : i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                if (m_hasBackground && i == m_masks.size()) {
                    emit maskVisibleChanged(-1, item->checkState() == Qt::Checked); // Background row
                    return;
                }
                emit maskVisibleChanged(i, item->checkState() == Qt::Checked);
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &,
                   int destRow) {
                if (m_syncing) return;
                const int bgRow = m_masks.size(); // Background is pinned to the last row
                if (m_hasBackground && (start >= bgRow || destRow > bgRow))
                    return; // can't move Background, or drop a layer below it
                int to = destRow > start ? destRow - 1 : destRow;
                emit maskReorderRequested(start, to);
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

    connect(m_shapeList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (m_syncing || !item) return;
                int idx = item->data(0, Qt::UserRole).toInt();
                // A group's parent row has no shape of its own; select its
                // first child instead — RetouchTab::selectShape already
                // expands a grouped shape's selection to the whole group.
                if (idx < 0 && item->childCount() > 0)
                    idx = item->child(0)->data(0, Qt::UserRole).toInt();
                if (idx >= 0) emit selectShapeRequested(idx);
            });
    connect(m_shapeList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int) {
        if (m_syncing) return;
        int idx = item->data(0, Qt::UserRole).toInt();
        if (idx >= 0) emit shapeVisibleChanged(idx, item->checkState(0) == Qt::Checked);
    });
    connect(m_groupShapes, &QPushButton::clicked, this,
            [this] { emit groupShapesRequested(); });
    connect(m_ungroupShapes, &QPushButton::clicked, this,
            [this] { emit ungroupShapesRequested(); });

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
    m_shapes.clear();
    m_activeShape = -1;
    m_removals.clear();
    m_activeRemoval = -1;
    m_syncing = true;
    m_maskList->clear();
    m_shapeList->clear();
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
                            bool backgroundHidden) {
    m_masks = masks;
    m_active = activeIndex;
    m_hasBackground = hasBackground;
    m_backgroundHidden = backgroundHidden;
    setEnabled(true);
    rebuildList();
    loadActive();
}

void LayersPanel::setShapes(const QVector<ShapeOp> &shapes, int activeIndex) {
    m_shapes = shapes;
    m_activeShape = activeIndex;
    setEnabled(true);
    rebuildShapeList();
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
            m_shapesSectionDock, m_removalsSectionDock};
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

void LayersPanel::rebuildList() {
    m_syncing = true;
    m_maskList->clear();
    for (int i = 0; i < m_masks.size(); ++i) {
        const Mask &m = m_masks[i];
        QString label = m.name.isEmpty()
                            ? QString("Layer %1 (%2)").arg(i + 1).arg(maskTypeLabel(m.type))
                            : m.name;
        if (m.isImageLayer()) {
            if (m.sourceMissing) label += " (missing)";
            else if (m.sourceImageCache.isNull()) label += " (loading…)";
        }
        auto *item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
        m_maskList->addItem(item);
    }
    if (m_hasBackground) {
        // Pinned to the bottom of the stack, like Photoshop's Background layer.
        // Unlike other rows it can't be dragged, but it can be hidden (eye
        // checkbox) and deleted (see loadActive()'s isBackground handling).
        auto *bg = new QListWidgetItem(QStringLiteral("Background \xF0\x9F\x94\x92")); // trailing lock emoji
        bg->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        bg->setCheckState(m_backgroundHidden ? Qt::Unchecked : Qt::Checked);
        m_maskList->addItem(bg);
    }
    const int row = (m_active >= 0) ? m_active
                    : (m_hasBackground ? m_masks.size() : -1);
    if (row >= 0 && row < m_maskList->count())
        m_maskList->setCurrentRow(row);
    m_syncing = false;
}

// Builds the tree top-to-bottom to match the stack's top-to-bottom render
// order (last-drawn/topmost shape first): walks Adjustments::shapes from the
// highest index down, creating a "Group" parent row the first time each
// groupId is seen and nesting every subsequent same-group shape under it
// (safe because RetouchTab::groupSelectedShapes keeps a group's members
// contiguous, so a group's block is never interrupted by another shape).
void LayersPanel::rebuildShapeList() {
    m_syncing = true;
    m_shapeList->clear();
    QHash<QString, QTreeWidgetItem *> groupItems;
    for (int i = m_shapes.size() - 1; i >= 0; --i) {
        const ShapeOp &s = m_shapes[i];
        QTreeWidgetItem *parent = nullptr;
        if (!s.groupId.isEmpty()) {
            auto it = groupItems.constFind(s.groupId);
            if (it == groupItems.constEnd()) {
                auto *g = new QTreeWidgetItem(m_shapeList, {QStringLiteral("Group")});
                g->setData(0, Qt::UserRole, -1);
                groupItems.insert(s.groupId, g);
                parent = g;
            } else {
                parent = it.value();
            }
        }
        QString label = shapeTypeLabel(s.type) + " " + QString::number(i + 1);
        auto *item = parent ? new QTreeWidgetItem(parent, {label})
                             : new QTreeWidgetItem(m_shapeList, {label});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, s.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, i);
        if (i == m_activeShape) m_shapeList->setCurrentItem(item);
    }
    m_shapeList->expandAll();
    m_syncing = false;
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
