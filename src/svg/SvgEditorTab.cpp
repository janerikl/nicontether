#include "svg/SvgEditorTab.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "svg/SvgCanvas.h"
#include "svg/SvgFileIO.h"
#include "svg/SvgRender.h"

namespace {
QString swatchStyle(const QColor &c) {
    return QString("background-color: %1; border: 1px solid #555;").arg(c.name(QColor::HexArgb));
}
} // namespace

SvgEditorTab::SvgEditorTab(QWidget *parent) : QWidget(parent) {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_canvas = new SvgCanvas(this);

    buildTopBar(rootLayout);

    auto *middleLayout = new QHBoxLayout;
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    buildLeftRail(middleLayout);
    middleLayout->addWidget(m_canvas, 1);
    buildRightPanel(middleLayout);

    rootLayout->addLayout(middleLayout, 1);

    connect(m_canvas, &SvgCanvas::documentChanged, this, &SvgEditorTab::refreshLayersList);
    connect(m_canvas, &SvgCanvas::selectionChanged, this, [this] {
        refreshLayersList();
        refreshAppearanceFromSelection();
    });
    refreshLayersList();
    refreshAppearanceFromSelection();
}

void SvgEditorTab::buildTopBar(QVBoxLayout *rootLayout) {
    auto *topBar = new QToolBar(this);
    topBar->setMovable(false);
    topBar->setIconSize(QSize(16, 16));
    // Same hover/checked treatment as RetouchWindow's main tools toolbar
    // (RetouchWindow.cpp), no separate background color of its own.
    topBar->setStyleSheet("QToolButton { border: none; padding: 4px 8px; border-radius: 4px; }"
                           "QToolButton:hover { background: rgba(220,220,220,0.85); }");

    auto *openAction = topBar->addAction("Open…");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &SvgEditorTab::onOpen);

    auto *saveAction = topBar->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &SvgEditorTab::onSave);

    auto *saveAsAction = topBar->addAction("Save As…");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &SvgEditorTab::onSaveAs);

    auto *exportPngAction = topBar->addAction("Export PNG…");
    connect(exportPngAction, &QAction::triggered, this, &SvgEditorTab::onExportPng);

    topBar->addSeparator();
    auto *sendToRetouchAction = topBar->addAction("Send to Retouch as Layer");
    connect(sendToRetouchAction, &QAction::triggered, this, [this] {
        QImage image = renderSvgDocumentToImage(m_canvas->document(), 3.0);
        QString name = m_currentPath.isEmpty()
            ? QStringLiteral("SVG Layer")
            : QFileInfo(m_currentPath).completeBaseName();
        emit sendToRetouchRequested(image, name);
    });

    rootLayout->addWidget(topBar);
}

void SvgEditorTab::buildLeftRail(QHBoxLayout *middleLayout) {
    // Narrow dark vertical tool rail, in the spirit of Illustrator's toolbox.
    auto *rail = new QToolBar(this);
    rail->setOrientation(Qt::Vertical);
    rail->setMovable(false);
    rail->setIconSize(QSize(20, 20));
    rail->setFixedWidth(44);
    // Same checked/hover colors as RetouchWindow's main tools toolbar
    // (RetouchWindow.cpp) so the active-tool highlight matches Retouch mode.
    rail->setStyleSheet("QToolBar { border: none; spacing: 2px; padding: 4px 0; }"
                         "QToolButton { padding: 6px; border-radius: 4px; }"
                         "QToolButton:hover { background: rgba(220,220,220,0.85); }"
                         "QToolButton:checked { background: #3a3f47; color: white; }");

    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    auto addToolAction = [&](const QString &label, const QString &shortLabel, SvgToolMode mode) {
        QAction *action = rail->addAction(shortLabel);
        action->setToolTip(label);
        action->setCheckable(true);
        toolGroup->addAction(action);
        connect(action, &QAction::triggered, m_canvas, [this, mode] { m_canvas->setToolMode(mode); });
        return action;
    };

    addToolAction("Select (V)", "V", SvgToolMode::Select)->setChecked(true);
    addToolAction("Pen (P)", "P", SvgToolMode::Pen);
    addToolAction("Text (T)", "T", SvgToolMode::Text);
    rail->addSeparator();
    addToolAction("Rectangle (R)", "▭", SvgToolMode::Rect);
    addToolAction("Ellipse (O)", "◯", SvgToolMode::Ellipse);
    addToolAction("Line (\\)", "／", SvgToolMode::Line);
    addToolAction("Polygon", "⬠", SvgToolMode::Polygon);
    addToolAction("Star", "★", SvgToolMode::Star);

    middleLayout->addWidget(rail);
}

void SvgEditorTab::buildRightPanel(QHBoxLayout *middleLayout) {
    auto *panel = new QWidget(this);
    panel->setFixedWidth(240);
    // No background override — inherits the native palette, same as every
    // other dock/panel in the app (LayersPanel, MaskPanel, etc.).
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->setSpacing(10);

    // Appearance: fill / stroke, mirroring Illustrator's top-of-panel swatches.
    auto *appearanceBox = new QGroupBox("Appearance", panel);
    auto *appearanceForm = new QFormLayout(appearanceBox);

    m_fillSwatch = new QToolButton(appearanceBox);
    m_fillSwatch->setFixedSize(28, 28);
    m_fillSwatch->setStyleSheet(swatchStyle(QColor(200, 200, 200)));
    connect(m_fillSwatch, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(Qt::white, this, "Fill Color", QColorDialog::ShowAlphaChannel);
        if (c.isValid()) m_canvas->setFillColor(c);
    });
    appearanceForm->addRow("Fill", m_fillSwatch);

    auto *gradientButton = new QToolButton(appearanceBox);
    gradientButton->setText("Gradient");
    connect(gradientButton, &QToolButton::clicked, this, [this] {
        SvgGradient gradient;
        gradient.type = SvgGradientType::Linear;
        gradient.stops = {{0.0, QColor(255, 120, 80)}, {1.0, QColor(80, 120, 255)}};
        m_canvas->setFillGradient(gradient);
    });
    appearanceForm->addRow("", gradientButton);

    m_strokeSwatch = new QToolButton(appearanceBox);
    m_strokeSwatch->setFixedSize(28, 28);
    m_strokeSwatch->setCheckable(true);
    m_strokeSwatch->setStyleSheet(swatchStyle(Qt::black));
    m_strokeWidthSpin = new QDoubleSpinBox(appearanceBox);
    m_strokeWidthSpin->setRange(0.0, 200.0);
    m_strokeWidthSpin->setValue(2.0);
    auto applyStroke = [this] {
        m_canvas->setStroke(m_strokeSwatch->isChecked(), QColor(m_strokeSwatch->property("color").toString()),
                             m_strokeWidthSpin->value());
    };
    connect(m_strokeSwatch, &QToolButton::clicked, this, [this, applyStroke] {
        if (m_strokeSwatch->isChecked()) {
            QColor c = QColorDialog::getColor(Qt::black, this, "Stroke Color");
            if (c.isValid()) {
                m_strokeSwatch->setProperty("color", c.name(QColor::HexArgb));
                m_strokeSwatch->setStyleSheet(swatchStyle(c));
            }
        }
        applyStroke();
    });
    connect(m_strokeWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [applyStroke] { applyStroke(); });
    appearanceForm->addRow("Stroke", m_strokeSwatch);
    appearanceForm->addRow("Width", m_strokeWidthSpin);

    panelLayout->addWidget(appearanceBox);

    // Arrange: group/ungroup, align, boolean ops.
    auto *arrangeBox = new QGroupBox("Arrange", panel);
    auto *arrangeLayout = new QVBoxLayout(arrangeBox);

    auto *groupRow = new QHBoxLayout;
    auto *groupButton = new QToolButton(arrangeBox);
    groupButton->setText("Group");
    connect(groupButton, &QToolButton::clicked, this, [this] { m_canvas->groupSelection(); });
    auto *ungroupButton = new QToolButton(arrangeBox);
    ungroupButton->setText("Ungroup");
    connect(ungroupButton, &QToolButton::clicked, this, [this] { m_canvas->ungroupSelection(); });
    groupRow->addWidget(groupButton);
    groupRow->addWidget(ungroupButton);
    arrangeLayout->addLayout(groupRow);

    auto *alignButton = new QToolButton(arrangeBox);
    alignButton->setText("Align");
    alignButton->setPopupMode(QToolButton::InstantPopup);
    auto *alignMenu = new QMenu(alignButton);
    auto addAlignAction = [&](const QString &label, SvgAlignEdge edge) {
        QAction *action = alignMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, edge] { m_canvas->alignSelection(edge); });
    };
    addAlignAction("Left", SvgAlignEdge::Left);
    addAlignAction("Center Horizontally", SvgAlignEdge::HCenter);
    addAlignAction("Right", SvgAlignEdge::Right);
    addAlignAction("Top", SvgAlignEdge::Top);
    addAlignAction("Center Vertically", SvgAlignEdge::VCenter);
    addAlignAction("Bottom", SvgAlignEdge::Bottom);
    alignButton->setMenu(alignMenu);
    arrangeLayout->addWidget(alignButton);

    auto *booleanButton = new QToolButton(arrangeBox);
    booleanButton->setText("Boolean Op");
    booleanButton->setPopupMode(QToolButton::InstantPopup);
    auto *booleanMenu = new QMenu(booleanButton);
    auto addBoolAction = [&](const QString &label, SvgBooleanOp op) {
        QAction *action = booleanMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, op] { m_canvas->applyBooleanOp(op); });
    };
    addBoolAction("Union", SvgBooleanOp::Union);
    addBoolAction("Subtract", SvgBooleanOp::Subtract);
    addBoolAction("Intersect", SvgBooleanOp::Intersect);
    addBoolAction("Xor", SvgBooleanOp::Xor);
    booleanButton->setMenu(booleanMenu);
    arrangeLayout->addWidget(booleanButton);

    panelLayout->addWidget(arrangeBox);

    // Layers: topmost node listed first, matching Illustrator's Layers panel.
    auto *layersBox = new QGroupBox("Layers", panel);
    auto *layersLayout = new QVBoxLayout(layersBox);
    m_layersList = new QListWidget(layersBox);
    connect(m_layersList, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_syncingUi) return;
        QSet<QString> ids;
        for (QListWidgetItem *item : m_layersList->selectedItems())
            ids.insert(item->data(Qt::UserRole).toString());
        m_canvas->setSelection(ids);
    });
    layersLayout->addWidget(m_layersList);
    panelLayout->addWidget(layersBox, 1);

    middleLayout->addWidget(panel);
}

void SvgEditorTab::refreshLayersList() {
    if (!m_layersList) return;
    m_syncingUi = true;
    m_layersList->clear();
    const SvgDocument &doc = m_canvas->document();
    for (int i = doc.nodes.size() - 1; i >= 0; --i) {
        const SvgNode &node = doc.nodes[i];
        auto *item = new QListWidgetItem(node.name.isEmpty() ? QStringLiteral("Layer") : node.name, m_layersList);
        item->setData(Qt::UserRole, node.id);
        item->setSelected(m_canvas->selection().contains(node.id));
    }
    m_syncingUi = false;
}

void SvgEditorTab::refreshAppearanceFromSelection() {
    if (!m_fillSwatch) return;
    const QSet<QString> &sel = m_canvas->selection();
    if (sel.isEmpty()) return;
    const SvgNode *node = m_canvas->document().findById(*sel.begin());
    if (!node) return;

    m_syncingUi = true;
    m_fillSwatch->setStyleSheet(swatchStyle(
        node->fillType == SvgFillType::Solid ? node->fillColor : QColor(200, 200, 200)));

    QSignalBlocker blockStroke(m_strokeSwatch);
    m_strokeSwatch->setChecked(node->strokeEnabled);
    m_strokeSwatch->setProperty("color", node->strokeColor.name(QColor::HexArgb));
    m_strokeSwatch->setStyleSheet(swatchStyle(node->strokeColor));

    QSignalBlocker blockWidth(m_strokeWidthSpin);
    m_strokeWidthSpin->setValue(node->strokeWidth);
    m_syncingUi = false;
}

bool SvgEditorTab::saveToPath(const QString &path) {
    QString error;
    if (!SvgFileIO::save(m_canvas->document(), path, &error)) {
        QMessageBox::warning(this, "Save Failed", error);
        return false;
    }
    m_currentPath = path;
    return true;
}

void SvgEditorTab::onOpen() {
    QString path = QFileDialog::getOpenFileName(this, "Open SVG", QString(), "SVG Files (*.svg)");
    if (path.isEmpty()) return;
    QString error;
    SvgDocument doc;
    if (!SvgFileIO::load(doc, path, &error)) {
        QMessageBox::warning(this, "Open Failed", error);
        return;
    }
    m_canvas->document() = doc;
    m_canvas->clearSelection();
    m_canvas->update();
    m_currentPath = path;
    refreshLayersList();
}

void SvgEditorTab::onSave() {
    if (m_currentPath.isEmpty()) {
        onSaveAs();
        return;
    }
    saveToPath(m_currentPath);
}

void SvgEditorTab::onSaveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save SVG", QString(), "SVG Files (*.svg)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".svg", Qt::CaseInsensitive)) path += ".svg";
    saveToPath(path);
}

void SvgEditorTab::onExportPng() {
    QString path = QFileDialog::getSaveFileName(this, "Export PNG", QString(), "PNG Files (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    QImage image = renderSvgDocumentToImage(m_canvas->document(), 4.0);
    if (!image.save(path)) {
        QMessageBox::warning(this, "Export Failed", "Could not write PNG file.");
    }
}
