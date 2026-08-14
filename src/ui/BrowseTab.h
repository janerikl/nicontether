#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QPushButton;

// Visual browser for picking photos to open in Retouch. Left side lists
// capture sessions (dated folders under SessionManager's base dir, plus
// recent session folders) and recently opened .ploom projects; selecting a
// session shows a thumbnail grid of its photos on the right, while selecting
// a project opens it directly (a project is a single edit, not a folder).
// Thumbnails reuse the same cached-thumbnail-then-embedded-preview strategy
// as the Retouch filmstrip, generated off the GUI thread.
class BrowseTab : public QWidget {
    Q_OBJECT
public:
    explicit BrowseTab(QWidget *parent = nullptr);

    // Repopulate the sessions/projects list from disk + QSettings. Call when
    // the Browse tab becomes visible so it reflects the latest sessions.
    void refresh();

signals:
    // One or more photos/projects were chosen to open in Retouch.
    void openRequested(const QStringList &paths);

private:
    void populateSourceList();
    void loadFolder(const QString &dir);
    void onSourceActivated(QListWidgetItem *item);
    void onGridActivated(QListWidgetItem *item);
    void onOpenClicked();
    void browseForFolder();

    QListWidget *m_sourceList = nullptr;
    QListWidget *m_grid = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_browseButton = nullptr;
    QString m_currentDir;
};
