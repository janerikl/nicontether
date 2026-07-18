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
class QLabel;

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
    RetouchTab *currentTab() const;
    void syncDockFromTab();
    void setDockEnabled(bool enabled);

    QTabWidget *m_tabs = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    QSet<QString> m_filmstripPaths;
    QMap<QString, RetouchTab *> m_openTabs;

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
    QPushButton *m_cropToggle = nullptr;
    QPushButton *m_cropApply = nullptr;
    QPushButton *m_cropReset = nullptr;
    class QComboBox *m_cropAspect = nullptr;
    QPushButton *m_healToggle = nullptr;
    QSlider *m_healBrush = nullptr;
    QPushButton *m_healClear = nullptr;
    QLabel *m_statusLabel = nullptr;

    ExportPresetStore m_presetStore;
    bool m_syncing = false; // guard against feedback while loading dock from tab
};
