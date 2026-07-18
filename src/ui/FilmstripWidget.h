#pragma once

#include <QListWidget>
#include <QImage>

// Horizontal thumbnail strip of captured frames. Emits the file path when a
// thumbnail is activated so the main window can show a large preview.
class FilmstripWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FilmstripWidget(QWidget *parent = nullptr);

    // Save-state badge drawn over a thumbnail.
    enum Badge { NoBadge = 0, Saved, Unsaved };

    void addCapture(const QString &path, const QImage &preview);
    void setBadge(const QString &path, Badge state);

    // Paths of all multi-selected thumbnails (for sync-edits across a shoot).
    QStringList selectedPaths() const;

signals:
    void frameSelected(const QString &path);
    void retouchRequested(const QString &path);
    void syncEditsRequested(); // "Sync Edits to Selected" chosen from the menu

protected:
    void contextMenuEvent(QContextMenuEvent *) override;
};
