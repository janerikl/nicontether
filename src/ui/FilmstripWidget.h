#pragma once

#include <QListWidget>
#include <QImage>
#include <QPixmap>

class QMimeData;

// Horizontal thumbnail strip of captured frames. Emits the file path when a
// thumbnail is activated so the main window can show a large preview. Also a
// drag source: dragging a thumbnail out exposes its file as a standard
// text/uri-list, e.g. to drop it onto the retoucher's canvas as an image layer.
class FilmstripWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FilmstripWidget(QWidget *parent = nullptr);

    // Save-state badge drawn over a thumbnail.
    enum Badge { NoBadge = 0, Saved, Unsaved };

    void addCapture(const QString &path, const QImage &preview, int rating = 0);
    // Replace an existing thumbnail's icon (e.g. to reflect the latest edits).
    // No-op if no item matches the path.
    void updateThumbnail(const QString &path, const QImage &image);
    void setBadge(const QString &path, Badge state);
    // Set the star rating (0-5) shown on a thumbnail. 0 clears it.
    void setRating(const QString &path, int rating);
    // Update a thumbnail's path and displayed filename in place after an
    // on-disk rename. No-op if no item matches oldPath.
    void renamePath(const QString &oldPath, const QString &newPath);

    // Paths of all multi-selected thumbnails (for sync-edits across a shoot).
    QStringList selectedPaths() const;

signals:
    void frameSelected(const QString &path);
    void syncEditsRequested(); // "Sync Edits to Selected" chosen from the menu
    void deleteRequested(const QStringList &paths); // "Delete" chosen from the menu
    void renameRequested(const QString &path); // "Rename" chosen from the menu
    void ratingChanged(const QString &path, int rating); // star chosen from the Rating submenu (0 = clear)

protected:
    void contextMenuEvent(QContextMenuEvent *) override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;

private:
    // Scale an image to a thumbnail pixmap, or a gray placeholder if null.
    QPixmap iconFor(const QImage &image) const;
};
