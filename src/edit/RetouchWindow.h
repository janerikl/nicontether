#pragma once

#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QString>

#include "edit/Adjustments.h"
#include "edit/ExportPreset.h"

class RetouchTab;
class FilmstripWidget;
class CurveEditor;
class QTabWidget;
class QSlider;
class QPushButton;
class QToolButton;
class FlyoutToolButton;
class QLabel;
class QToolBar;
class QStackedWidget;
class QDockWidget;
class QListWidget;
class TetherView;
class PreferencesDialog;
class LevelsPanel;
class MaskPanel;
class LayersPanel;

// Separate top-level window for retouching photos. Own filmstrip selector plus
// one tab per open photo, an adjustments dock, and JPEG/PNG export.
class RetouchWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit RetouchWindow(QWidget *parent = nullptr);

    // Add a photo to the selector filmstrip (thumbnail via embedded JPEG). No-op
    // if already present.
    void addToFilmstrip(const QString &path);
    // Open the photo in a tab (or activate its existing tab).
    void openPhoto(const QString &path);

    enum class Mode { Retouch, Tether };
    void setMode(Mode mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onOpenSession();
    void onOpenPhotos();
    void onSave();
    void onSaveAll();
    void onFilmstripSelected(const QString &path);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onDeleteRequested(const QStringList &paths);
    void onToneChanged();
    void onExport();

private:
    void buildDock();
    void loadSession(const QString &dir);      // scan a folder for NEFs into the filmstrip
    void rebuildRecentSessionsMenu();          // repopulate the recent-session items in the File menu
    void buildToolPanel(); // narrow left icon toolbar: Zoom / Crop / Spot Heal tools
    void deselectAllTools(); // uncheck all left-bar tools and exit their modes
    void buildToolOptionsBar(); // contextual per-tool options row under the main toolbar
    RetouchTab *currentTab() const;
    void syncDockFromTab();
    void setDockEnabled(bool enabled);
    void applyModeChrome(Mode mode);
    class QAction *m_tetherModeAction = nullptr;
    class QAction *m_retouchModeAction = nullptr;

    QTabWidget *m_tabs = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    QSet<QString> m_filmstripPaths;
    QMap<QString, RetouchTab *> m_openTabs;

    // Unified window: central stack swaps editing tabs (page 0) / tether (page 1).
    QStackedWidget *m_modeStack = nullptr;
    TetherView *m_tetherView = nullptr;
    PreferencesDialog *m_prefsDialog = nullptr;
    QDockWidget *m_controlsDock = nullptr; // camera controls, shown in Tether mode
    QToolBar *m_tetherToolBar = nullptr;   // Connect/Disconnect/LiveView/Capture/…

    // Promoted from constructor locals so mode chrome can enable/disable them.
    class QAction *m_saveAction = nullptr;
    class QAction *m_saveAllAction = nullptr;
    class QAction *m_exportAction = nullptr;

    // File menu + anchors for the rebuildable recent-sessions section. The
    // recent items live between these two separators (inserted before
    // m_recentEndSeparator); both separators are hidden when the list is empty.
    class QMenu *m_fileMenu = nullptr;
    class QAction *m_recentBeginSeparator = nullptr; // separator above the recent items
    class QAction *m_recentEndSeparator = nullptr;   // separator below the recent items
    QList<class QAction *> m_recentActions;          // current recent-session menu entries

    // View menu togglable panels.
    QToolBar *m_toolsBar = nullptr; // left icon bar: Zoom / Crop / Spot Heal
    QDockWidget *m_adjustmentsDock = nullptr;
    QDockWidget *m_historyDock = nullptr;
    QListWidget *m_historyList = nullptr;
    QDockWidget *m_levelsDock = nullptr;
    LevelsPanel *m_levelsPanel = nullptr;
    void buildViewMenu();
    void applyDefaultDockLayout(); // re-apply the default dock arrangement (used on first launch + Reset Panels)
    void buildHistoryDock();
    void buildLevelsDock();
    void refreshHistoryPanel();  // rebuild the list from the current tab
    void refreshLevels();        // push the current tab's preview + levels into the panel

    // Dock controls.
    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_highlights = nullptr;
    QSlider *m_shadows = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_vibrance = nullptr;
    QSlider *m_temperature = nullptr;
    QSlider *m_tint = nullptr;
    QSlider *m_clarity = nullptr;
    QSlider *m_sharpen = nullptr;
    QSlider *m_vignette = nullptr;
    CurveEditor *m_curve = nullptr;
    QPushButton *m_wbPick = nullptr;
    QPushButton *m_beforeAfter = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QPushButton *m_zoomFit = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QPushButton *m_rotLeft = nullptr;
    QPushButton *m_rotRight = nullptr;
    QPushButton *m_flipH = nullptr;
    QPushButton *m_flipV = nullptr;
    QToolButton *m_toolZoom = nullptr;  // left icon bar: zoom tool (marquee/Ctrl+wheel)
    QToolButton *m_cropToggle = nullptr; // left icon bar: crop tool
    QPushButton *m_cropApply = nullptr;
    QPushButton *m_cropReset = nullptr;
    class QComboBox *m_cropAspect = nullptr;
    QToolButton *m_healToggle = nullptr; // left icon bar: spot-heal tool
    QSlider *m_healBrush = nullptr;
    QPushButton *m_healClear = nullptr;
    FlyoutToolButton *m_maskToggle = nullptr; // left icon bar: local-mask tool
    QDockWidget *m_layersDock = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    QDockWidget *m_maskDock = nullptr;
    MaskPanel *m_maskPanel = nullptr;
    void buildLayersDock();
    void buildMaskDock();
    void refreshMaskPanel(); // refreshes both Layers and Masks panels
    // Local-mask subtool selected via the tool's Photoshop-style flyout; a plain
    // click on the mask tool creates a mask of this type.
    MaskType m_activeMaskSubtool = MaskType::Radial;
    void openMaskFlyout();       // pop the subtool strip next to the mask button
    void setMaskSubtool(MaskType t); // set active subtool + refresh tool glyph
    void addActiveMask();        // create a mask of the active subtool
    QLabel *m_statusLabel = nullptr;
    class QAction *m_undoAction = nullptr;
    class QAction *m_redoAction = nullptr;

    // Copy/paste/sync of portable edits (tone/colour/detail/curve/levels) across
    // photos. Geometry and spot-heals are image-specific and never copied.
    class QAction *m_copyEditsAction = nullptr;
    class QAction *m_pasteEditsAction = nullptr;
    class QAction *m_syncEditsAction = nullptr;
    Adjustments m_editClipboard;
    bool m_hasEditClipboard = false;
    void onCopyEdits();
    void onPasteEdits();
    void onSyncEdits();
    void updateEditClipboardActions();
    // Overwrite dst's portable fields from src, preserving dst geometry & heals.
    static void mergePortable(const Adjustments &src, Adjustments &dst);
    // Apply the clipboard to one photo (open tab or closed sidecar). Returns
    // true if the photo changed. Reports counts to the caller.
    bool applyClipboardTo(const QString &path);

    // Contextual per-tool options row (shown under the main toolbar only while
    // a left-bar tool is selected).
    QToolBar *m_toolOptionsBar = nullptr;
    QStackedWidget *m_toolOptionsStack = nullptr;

    ExportPresetStore m_presetStore;
    bool m_syncing = false; // guard against feedback while loading dock from tab
};
