#pragma once

#include <QWidget>

#include "edit/Adjustments.h"

class QComboBox;
class QCheckBox;
class QSlider;
class QLabel;
class QLineEdit;
class QFontComboBox;
class QSpinBox;
class QToolButton;
class BrushPresetMenuButton;

// Mask editor for whichever layer is selected in the Layers panel: a type
// combo (None / Radial / Graduated / Brush) to add, remove, or change the
// layer's mask, plus its shape controls (invert / feather / brush hardness &
// size / auto mask). Separate from LayersPanel — this dock is purely about
// *where* a layer's adjustment applies, not the adjustment itself. Purely a
// view — it emits intent signals and is refreshed via setMask(); RetouchWindow
// routes to the tab.
class MaskPanel : public QWidget {
    Q_OBJECT
public:
    explicit MaskPanel(QWidget *parent = nullptr);

    void setMask(const Mask &mask, bool hasSelection);
    void clear();
    // Reflect a brush radius change (e.g. ctrl+wheel on the canvas) without
    // a full resync.
    void setBrushRadius(double radiusNorm);
    // Current tab's image width in px, so the brush size readout can convert
    // the (width-normalized) slider value into an actual pixel size.
    void setImageWidth(int width);

signals:
    void maskTypeChanged(MaskType type);
    void maskShapeChanged(bool inverted, double feather, double hardness,
                          double brushRadius, bool autoMask);
    // Text-type mask content/style (see Mask::text and friends). `pixelSize`
    // is width-normalized, same convention as brushRadius.
    void maskTextChanged(const QString &text, const QString &family, double pixelSize,
                         bool bold, bool italic);

private:
    void emitShape();
    void loadMask();
    void updateBrushSizePxLabel();

    Mask m_mask;
    bool m_hasSelection = false;
    bool m_syncing = false;
    int m_imageWidth = 0;

    QLabel *m_hint = nullptr;
    QWidget *m_typeSection = nullptr; // the "Mask:" combo row; hidden for Shape/TextBox layers
    QComboBox *m_type = nullptr;

    QCheckBox *m_invert = nullptr;
    QSlider *m_feather = nullptr;
    QLabel *m_hardnessLabel = nullptr;
    QSlider *m_hardness = nullptr;
    QLabel *m_brushSizeLabel = nullptr;
    QSlider *m_brushSize = nullptr;
    QLabel *m_brushSizePx = nullptr; // live "NNpx" readout next to m_brushSize
    QCheckBox *m_autoMask = nullptr;
    BrushPresetMenuButton *m_brushPresets = nullptr;

    QLineEdit *m_textContent = nullptr;
    QFontComboBox *m_textFont = nullptr;
    QSpinBox *m_textSize = nullptr; // percent of image width
    QToolButton *m_textBold = nullptr;
    QToolButton *m_textItalic = nullptr;
    void emitText();
};
