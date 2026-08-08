#pragma once

#include <QImage>
#include <QWidget>

class SvgCanvas;
class QListWidget;
class QToolButton;
class QLabel;
class QDoubleSpinBox;

// Vector editor for creating icons/logos. Lives as a persistent page in
// RetouchWindow's mode QStackedWidget (index 2), switched to via the "SVG"
// toolbar button alongside the existing Retouch/Tether mode buttons — a
// single long-lived workspace, not a per-document tab.
//
// Layout is modeled on Illustrator's classic three-pane arrangement: a
// narrow dark vertical tool rail on the left, the canvas in the middle, and
// a right-hand panel stacking Layers above Appearance (fill/stroke) and
// Arrange (group/align/boolean) controls. A slim top bar holds file actions.
class SvgEditorTab : public QWidget {
    Q_OBJECT

public:
    explicit SvgEditorTab(QWidget *parent = nullptr);

signals:
    // Emitted by "Send to Retouch as Layer", carrying a rasterized render of
    // the current document at export resolution. RetouchWindow decides
    // which Retouch document tab to add it to.
    void sendToRetouchRequested(QImage image, QString suggestedName);

private:
    void buildTopBar(class QVBoxLayout *rootLayout);
    void buildLeftRail(class QHBoxLayout *middleLayout);
    void buildRightPanel(class QHBoxLayout *middleLayout);
    void refreshLayersList();
    void refreshAppearanceFromSelection();

    void onOpen();
    void onSave();
    void onSaveAs();
    void onExportPng();
    bool saveToPath(const QString &path);

    SvgCanvas *m_canvas = nullptr;
    QString m_currentPath;

    QListWidget *m_layersList = nullptr;
    QToolButton *m_fillSwatch = nullptr;
    QToolButton *m_strokeSwatch = nullptr;
    QDoubleSpinBox *m_strokeWidthSpin = nullptr;
    bool m_syncingUi = false; // guards against feedback loops while refreshing UI from the canvas
};
