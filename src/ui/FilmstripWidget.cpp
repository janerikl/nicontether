#include "ui/FilmstripWidget.h"

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
    setItemDelegate(new BadgeDelegate(this));

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item) emit frameSelected(item->data(Qt::UserRole).toString());
    });
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

    QMenu menu(this);
    QAction *retouch = menu.addAction("Open in Retouch");
    if (menu.exec(ev->globalPos()) == retouch)
        emit retouchRequested(path);
}
