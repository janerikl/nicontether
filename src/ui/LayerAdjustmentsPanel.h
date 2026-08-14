#pragma once

#include <QWidget>

class QLabel;
class QStackedWidget;
class QListWidget;
class QPushButton;
class TonePanel;
class ColorPanel;
class ToneCurvePanel;
class LevelsPanel;
class DetailEffectsPanel;
class MaskPanel;

// Hosts the seven per-layer editing sections (Tone, Colour, Tone Curve,
// Levels, Detail & Effects, Masks, Remove Object) that used to live as
// always-visible collapsible QDockWidgets inside LayersPanel. Now only one
// section is shown at a time, in a QStackedWidget under a plain title label,
// switched via LayersPanel's right-click context menu on the layer list
// (see LayersPanel::sectionRequested). RetouchWindow docks this panel next
// to the Layers dock the first time a section is requested.
class LayerAdjustmentsPanel : public QWidget {
    Q_OBJECT
public:
    enum Section {
        Tone = 0,
        Colour,
        ToneCurveSection,
        Levels,
        DetailEffects,
        Masks,
        RemoveObject,
        SectionCount
    };

    explicit LayerAdjustmentsPanel(QWidget *parent = nullptr);

    void showSection(Section section);

    TonePanel *tonePanel() const { return m_tonePanel; }
    ColorPanel *colorPanel() const { return m_colorPanel; }
    ToneCurvePanel *toneCurvePanel() const { return m_toneCurvePanel; }
    LevelsPanel *levelsPanel() const { return m_levelsPanel; }
    DetailEffectsPanel *detailEffectsPanel() const { return m_detailEffectsPanel; }
    MaskPanel *maskPanel() const { return m_maskPanel; }
    QListWidget *removalList() const { return m_removalList; }
    QPushButton *deleteRemovalButton() const { return m_deleteRemoval; }

    static QString sectionTitle(Section section);

private:
    QLabel *m_header = nullptr;
    QStackedWidget *m_stack = nullptr;

    TonePanel *m_tonePanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    ToneCurvePanel *m_toneCurvePanel = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    DetailEffectsPanel *m_detailEffectsPanel = nullptr;
    MaskPanel *m_maskPanel = nullptr;

    QListWidget *m_removalList = nullptr;
    QPushButton *m_deleteRemoval = nullptr;
};
