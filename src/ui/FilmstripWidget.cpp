#include "ui/FilmstripWidget.h"

#include <QApplication>
#include <QPixmap>
#include <QFileInfo>
#include <QMenu>
#include <QContextMenuEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMimeData>
#include <QUrl>

namespace {
constexpr int kBadgeRole = Qt::UserRole + 1;
constexpr int kRatingRole = Qt::UserRole + 2;

// Paints a small save-state dot and a star rating over each thumbnail.
class BadgeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &index) const override {
        QStyledItemDelegate::paint(p, opt, index);
        int b = index.data(kBadgeRole).toInt();
        if (b != FilmstripWidget::NoBadge) {
            QColor c = (b == FilmstripWidget::Unsaved) ? QColor(255, 170, 0)
                                                       : QColor(150, 150, 150);
            const int d = 12;
            QRect dot(opt.rect.right() - d - 6, opt.rect.top() + 6, d, d);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(QPen(QColor(0, 0, 0, 180), 1));
            p->setBrush(c);
            p->drawEllipse(dot);
            p->restore();
        }

        int rating = index.data(kRatingRole).toInt();
        if (rating > 0) {
            QString stars;
            for (int i = 0; i < rating; ++i) stars += QChar(0x2605); // filled star
            p->save();
            QFont f = p->font();
            f.setPointSize(9);
            p->setFont(f);
            QRect textRect(opt.rect.left() + 4, opt.rect.top() + 93 - 16,
                            opt.rect.width() - 8, 16);
            p->setPen(Qt::black);
            p->drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignLeft | Qt::AlignVCenter, stars);
            p->setPen(QColor(255, 215, 0));
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, stars);
            p->restore();
        }
    }
};
} // namespace

FilmstripWidget::FilmstripWidget(QWidget *parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setIconSize(QSize(140, 93));
    setFixedHeight(140);
    setMovement(QListView::Static);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSpacing(4);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setItemDelegate(new BadgeDelegate(this));
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        // Plain click opens the photo; Ctrl/Shift-click only builds a selection
        // for sync (don't open a tab per modifier-click).
        if (item && QApplication::keyboardModifiers() == Qt::NoModifier)
            emit frameSelected(item->data(Qt::UserRole).toString());
    });
}

QStringList FilmstripWidget::selectedPaths() const {
    QStringList paths;
    const QList<QListWidgetItem *> items = selectedItems();
    for (QListWidgetItem *it : items)
        paths << it->data(Qt::UserRole).toString();
    return paths;
}

void FilmstripWidget::setBadge(const QString &path, Badge state) {
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (it->data(Qt::UserRole).toString() == path) {
            it->setData(kBadgeRole, int(state));
            viewport()->update();
            return;
        }
    }
}

void FilmstripWidget::setRating(const QString &path, int rating) {
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (it->data(Qt::UserRole).toString() == path) {
            it->setData(kRatingRole, rating);
            viewport()->update();
            return;
        }
    }
}

void FilmstripWidget::renamePath(const QString &oldPath, const QString &newPath) {
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (it->data(Qt::UserRole).toString() == oldPath) {
            it->setData(Qt::UserRole, newPath);
            it->setText(QFileInfo(newPath).fileName());
            return;
        }
    }
}

QPixmap FilmstripWidget::iconFor(const QImage &image) const {
    if (!image.isNull()) {
        return QPixmap::fromImage(image).scaled(
            iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Placeholder so a capture without an extractable preview still appears.
    QPixmap pix(iconSize());
    pix.fill(Qt::darkGray);
    return pix;
}

void FilmstripWidget::updateThumbnail(const QString &path, const QImage &image) {
    if (image.isNull()) return;
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (it->data(Qt::UserRole).toString() == path) {
            it->setIcon(QIcon(iconFor(image)));
            return;
        }
    }
}

void FilmstripWidget::addCapture(const QString &path, const QImage &preview, int rating) {
    QPixmap pix = iconFor(preview);

    auto *item = new QListWidgetItem(QIcon(pix), QFileInfo(path).fileName());
    item->setData(Qt::UserRole, path);
    if (rating > 0) item->setData(kRatingRole, rating);
    insertItem(0, item); // newest first
    setCurrentItem(item);
    scrollToTop();
}

QMimeData *FilmstripWidget::mimeData(const QList<QListWidgetItem *> &items) const {
    auto *data = QListWidget::mimeData(items);
    QList<QUrl> urls;
    for (QListWidgetItem *it : items)
        urls << QUrl::fromLocalFile(it->data(Qt::UserRole).toString());
    if (!urls.isEmpty()) data->setUrls(urls);
    return data;
}

void FilmstripWidget::contextMenuEvent(QContextMenuEvent *ev) {
    QListWidgetItem *item = itemAt(ev->pos());
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();

    // If the clicked thumbnail is part of the current multi-selection, the
    // action targets the whole selection; otherwise just the clicked one.
    QStringList delTargets;
    if (item->isSelected() && selectedItems().size() > 1)
        delTargets = selectedPaths();
    else
        delTargets = QStringList{path};

    QMenu menu(this);
    QAction *rename = menu.addAction("Rename");
    QMenu *ratingMenu = menu.addMenu("Rating");
    static const QString kStars[5] = {
        QStringLiteral("★☆☆☆☆"),
        QStringLiteral("★★☆☆☆"),
        QStringLiteral("★★★☆☆"),
        QStringLiteral("★★★★☆"),
        QStringLiteral("★★★★★"),
    };
    QAction *starActions[5];
    for (int i = 0; i < 5; ++i)
        starActions[i] = ratingMenu->addAction(kStars[i]);
    QAction *clearRating = ratingMenu->addAction("Clear Rating");
    const int selCount = selectedItems().size();
    QAction *sync = menu.addAction(
        selCount > 1 ? QString("Sync Edits to %1 Selected").arg(selCount)
                     : QString("Sync Edits to Selected"));
    QAction *del = menu.addAction(
        delTargets.size() > 1 ? QString("Delete %1 Photos").arg(delTargets.size())
                              : QString("Delete Photo"));
    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == rename)
        emit renameRequested(path);
    else if (chosen == clearRating)
        emit ratingChanged(path, 0);
    else if (chosen == sync)
        emit syncEditsRequested();
    else if (chosen == del)
        emit deleteRequested(delTargets);
    else {
        for (int i = 0; i < 5; ++i) {
            if (chosen == starActions[i]) {
                emit ratingChanged(path, i + 1);
                break;
            }
        }
    }
}
