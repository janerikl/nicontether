#pragma once

#include <QListWidget>
#include <QImage>
#include <QPixmap>

// Horizontal thumbnail strip of captured frames. Emits the file path when a
// thumbnail is activated so the main window can show a large preview.
class FilmstripWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FilmstripWidget(QWidget *parent = nullptr);

    // Save-state badge drawn over a thumbnail.
    enum Badge { NoBadge = 0, Saved, Unsaved };

    void addCapture(const QString &path, const QImage &preview);
    // Replace an existing thumbnail's icon (e.g. to reflect the latest edits).
    // No-op if no item matches the path.
    void updateThumbnail(const QString &path, const QImage &image);
    void setBadge(const QString &path, Badge state);

    // Paths of all multi-selected thumbnails (for sync-edits across a shoot).
    QStringList selectedPaths() const;

signals:
    void frameSelected(const QString &path);
    void retouchRequested(const QString &path);
    void syncEditsRequested(); // "Sync Edits to Selected" chosen from the menu
    void deleteRequested(const QStringList &paths); // "Delete" chosen from the menu

protected:
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    // Scale an image to a thumbnail pixmap, or a gray placeholder if null.
    QPixmap iconFor(const QImage &image) const;
};
