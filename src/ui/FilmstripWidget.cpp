#include "ui/FilmstripWidget.h"

#include <QApplication>
#include <QPixmap>
#include <QFileInfo>
#include <QMenu>
#include <QContextMenuEvent>
#include <QStyledItemDelegate>
#include <QPainter>

namespace {
constexpr int kBadgeRole = Qt::UserRole + 1;

// Paints a small save-state dot in the top-right of each thumbnail.
class BadgeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &index) const override {
        QStyledItemDelegate::paint(p, opt, index);
        int b = index.data(kBadgeRole).toInt();
        if (b == FilmstripWidget::NoBadge) return;
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

void FilmstripWidget::addCapture(const QString &path, const QImage &preview) {
    QPixmap pix;
    if (!preview.isNull()) {
        pix = QPixmap::fromImage(preview).scaled(
            iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        // Placeholder so a capture without an extractable preview still appears.
        pix = QPixmap(iconSize());
        pix.fill(Qt::darkGray);
    }

    auto *item = new QListWidgetItem(QIcon(pix), QFileInfo(path).fileName());
    item->setData(Qt::UserRole, path);
    insertItem(0, item); // newest first
    setCurrentItem(item);
    scrollToTop();
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
    QAction *retouch = menu.addAction("Open in Retouch");
    const int selCount = selectedItems().size();
    QAction *sync = menu.addAction(
        selCount > 1 ? QString("Sync Edits to %1 Selected").arg(selCount)
                     : QString("Sync Edits to Selected"));
    QAction *del = menu.addAction(
        delTargets.size() > 1 ? QString("Delete %1 Photos").arg(delTargets.size())
                              : QString("Delete Photo"));
    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == retouch)
        emit retouchRequested(path);
    else if (chosen == sync)
        emit syncEditsRequested();
    else if (chosen == del)
        emit deleteRequested(delTargets);
}
