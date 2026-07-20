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
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
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

QIcon drawFloatIcon() {
    QPixmap pm(kSectionIconPx, kSectionIconPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kSectionIconColor, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(2, 5, 7, 7));
    p.drawRect(QRectF(5, 2, 7, 7));
    return QIcon(pm);
}

// Custom title bar for a per-section dock: a chevron that collapses the
// dock down to just this bar (hides the content widget without closing the
// dock), a title label, a float button, and a close button. Replacing
// QDockWidget's default title bar this way means the native float/close
// buttons are gone, so this widget draws its own equivalents.
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

        auto *floatBtn = new QToolButton;
        floatBtn->setAutoRaise(true);
        floatBtn->setIcon(drawFloatIcon());
        floatBtn->setIconSize(QSize(kSectionIconPx, kSectionIconPx));
        floatBtn->setToolTip("Float this section");
        connect(floatBtn, &QToolButton::clicked, this, [this] {
            m_dock->setFloating(!m_dock->isFloating());
        });

        auto *closeBtn = new QToolButton;
        closeBtn->setAutoRaise(true);
        closeBtn->setIcon(drawCloseIcon());
        closeBtn->setIconSize(QSize(kSectionIconPx, kSectionIconPx));
        closeBtn->setToolTip("Close this section");
        connect(closeBtn, &QToolButton::clicked, m_dock, &QDockWidget::close);

        layout->addWidget(m_chevron);
        layout->addWidget(label, 1);
        layout->addWidget(floatBtn);
        layout->addWidget(closeBtn);

        setExpanded(false); // apply the initial collapsed state to the dock
    }

private:
    void setExpanded(bool expanded) {
        m_chevron->setIcon(drawChevronIcon(expanded));
        if (m_dock->widget()) m_dock->widget()->setVisible(expanded);
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
        dock->setFeatures(QDockWidget::DockWidgetClosable |
                          QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetMovable);
        dock->setWidget(content);
        content->setVisible(false); // collapsed by default
        dock->setTitleBarWidget(new SectionTitleBar(title, dock));
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
    m_levelsSectionDock = addSection("layerSectionLevels", "Levels", m_levelsPanel);
    m_detailEffectsPanel = new DetailEffectsPanel;
    m_detailEffectsSectionDock = addSection("layerSectionDetailEffects", "Detail & Effects",
                                            m_detailEffectsPanel);
    m_maskPanel = new MaskPanel;
    m_masksSectionDock = addSection("layerSectionMasks", "Masks", m_maskPanel);

    // Stack the six sections top-to-bottom.
    m_inner->splitDockWidget(m_toneSectionDock, m_colorSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_colorSectionDock, m_toneCurveSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_toneCurveSectionDock, m_levelsSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_levelsSectionDock, m_detailEffectsSectionDock, Qt::Vertical);
    m_inner->splitDockWidget(m_detailEffectsSectionDock, m_masksSectionDock, Qt::Vertical);

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
        if (!m_syncing) emit selectMaskRequested(i);
    });
    connect(m_maskList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (m_syncing) return;
                int i = m_maskList->row(item);
                emit maskVisibleChanged(i, item->checkState() == Qt::Checked);
            });
    connect(m_maskList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &,
                   int destRow) {
                if (m_syncing) return;
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
            [this](int clarity, int sharpen, int vignette) {
                m_curAdjust.clarity = clarity;
                m_curAdjust.sharpen = sharpen;
                m_curAdjust.vignette = vignette;
                emitAdjust();
            });
    connect(m_maskPanel, &MaskPanel::maskTypeChanged, this, &LayersPanel::maskTypeChanged);
    connect(m_maskPanel, &MaskPanel::maskShapeChanged, this, &LayersPanel::maskShapeChanged);

    clear();
}

void LayersPanel::clear() {
    m_masks.clear();
    m_active = -1;
    m_syncing = true;
    m_maskList->clear();
    m_syncing = false;
    setEnabled(false);
}

void LayersPanel::setMasks(const QVector<Mask> &masks, int activeIndex) {
    m_masks = masks;
    m_active = activeIndex;
    setEnabled(true);
    rebuildList();
    loadActive();
}

void LayersPanel::setLevelsPreviewImage(const QImage &img) {
    if (m_active >= 0 && m_active < m_masks.size()) m_levelsPanel->setImage(img);
}

void LayersPanel::setMaskBrushRadius(double radiusNorm) {
    if (m_maskPanel) m_maskPanel->setBrushRadius(radiusNorm);
}

QVector<QDockWidget *> LayersPanel::sectionDocks() const {
    return {m_toneSectionDock, m_colorSectionDock, m_toneCurveSectionDock,
            m_levelsSectionDock, m_detailEffectsSectionDock, m_masksSectionDock};
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
    if (m_active >= 0 && m_active < m_masks.size())
        m_maskList->setCurrentRow(m_active);
    m_syncing = false;
}

void LayersPanel::loadActive() {
    const bool has = m_active >= 0 && m_active < m_masks.size();
    m_syncing = true;
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_name, m_opacity, m_blend, m_duplicate, m_delete, m_levelsPanel})
        w->setEnabled(has);
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
                                              m_curAdjust.vignette);
        m_maskPanel->setMask(m, true);
    } else {
        m_curAdjust = MaskAdjust();
        m_tonePanel->clear();
        m_colorPanel->clear();
        m_toneCurvePanel->clear();
        m_levelsPanel->clear();
        m_detailEffectsPanel->clear();
        m_maskPanel->clear();
    }
    m_syncing = false;
}

void LayersPanel::emitAdjust() {
    if (m_syncing) return;
    emit maskAdjustChanged(m_curAdjust);
}
