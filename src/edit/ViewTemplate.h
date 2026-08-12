#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// A named snapshot of the main window's dock/toolbar arrangement, i.e. a
// QMainWindow::saveState() blob under a user-facing name. Selecting a
// template calls restoreState() with its blob (see RetouchWindow::
// applyViewTemplate). Built-in templates (Painting, Photo Editing) are
// generated on demand by RetouchWindow rather than stored, since their state
// blob depends on docks that only exist once the window is built; custom
// templates are persisted via QSettings.
struct ViewTemplate {
    QString name;
    QByteArray state; // QMainWindow::saveState(): dock/toolbar geometry + visibility
    // Extra chrome that QMainWindow::saveState() does not cover because it's
    // not a QDockWidget/QToolBar (a plain widget in the central layout, and a
    // per-tab canvas setting respectively).
    bool filmstripVisible = true;
    bool rulersVisible = false;
    bool builtIn = false;
};

// Persists custom view templates via QSettings. Mirrors AdjustmentPresetStore.
class ViewTemplateStore {
public:
    ViewTemplateStore() { load(); }

    const QList<ViewTemplate> &custom() const { return m_custom; }
    bool isCustom(const QString &name) const;

    void addOrUpdate(const ViewTemplate &t); // by name; persists
    void remove(const QString &name);        // custom only; persists
    void rename(const QString &oldName, const QString &newName); // custom only; persists

private:
    void load();
    void save() const;

    QList<ViewTemplate> m_custom;
};
