#pragma once

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
class QLabel;
class QToolBar;
class QStackedWidget;
class QDockWidget;
class QListWidget;
class TetherView;

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

private slots:
    void onOpenSession();
    void onOpenPhotos();
    void onSave();
    void onSaveAll();
    void onFilmstripSelected(const QString &path);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onToneChanged();
    void onExport();

private:
    void buildDock();
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
    QDockWidget *m_controlsDock = nullptr; // camera controls, shown in Tether mode
    QToolBar *m_tetherToolBar = nullptr;   // Connect/Disconnect/LiveView/Capture/…

    // Promoted from constructor locals so mode chrome can enable/disable them.
    class QAction *m_saveAction = nullptr;
    class QAction *m_saveAllAction = nullptr;
    class QAction *m_exportAction = nullptr;

    // View menu togglable panels.
    QToolBar *m_toolsBar = nullptr; // left icon bar: Zoom / Crop / Spot Heal
    QDockWidget *m_adjustmentsDock = nullptr;
    QDockWidget *m_historyDock = nullptr;
    QListWidget *m_historyList = nullptr;
    void buildViewMenu();
    void buildHistoryDock();
    void refreshHistoryPanel(); // rebuild the list from the current tab

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
    QLabel *m_statusLabel = nullptr;
    class QAction *m_undoAction = nullptr;
    class QAction *m_redoAction = nullptr;

    // Contextual per-tool options row (shown under the main toolbar only while
    // a left-bar tool is selected).
    QToolBar *m_toolOptionsBar = nullptr;
    QStackedWidget *m_toolOptionsStack = nullptr;

    ExportPresetStore m_presetStore;
    bool m_syncing = false; // guard against feedback while loading dock from tab
};
