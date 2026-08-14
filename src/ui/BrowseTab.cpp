#include "ui/BrowseTab.h"

#include "capture/NefPreview.h"
#include "capture/SessionManager.h"
#include "edit/EditSidecar.h"
#include "edit/RecentProjects.h"
#include "edit/RecentSessions.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSplitter>

namespace {

// Extensions offered by File > Open Photos, i.e. what a session/folder scan
// treats as a browsable image (RAW formats plus RawLoader::loadAny's
// standard-format fallback).
bool isBrowsableImage(const QFileInfo &fi) {
    static const QSet<QString> kExt = {
        "nef", "cr2", "cr3", "arw", "dng", "raf", "rw2", "orf",
        "jpg", "jpeg", "png", "tif", "tiff"};
    if (!kExt.contains(fi.suffix().toLower())) return false;

    // Exclude app-managed sidecar/internal files that live next to captures
    // in the same folder: EditSidecar's ".nte.json"/".nte.thumb.jpg", and
    // per-layer image assets copied in as "<name>.layer.<uuid>[.ext]" /
    // "<name>.svg-layer.<uuid>.png" (see EditSidecar.cpp, RetouchTab::
    // copyImageLayerAsset). None of these are top-level photos to browse.
    const QString name = fi.fileName();
    return !name.contains(".nte.") && !name.contains(".layer.") &&
           !name.contains("-layer.");
}

// Role used to stash the absolute file path on both list widgets' items.
constexpr int kPathRole = Qt::UserRole;
// Source-list item kinds: a session folder (loadFolder on activation) vs. a
// project file (opens directly, since a .ploom is a single edit not a
// folder of photos).
constexpr int kKindRole = Qt::UserRole + 1;
enum Kind { SessionKind, ProjectKind };

QPixmap iconFor(const QImage &image) {
    if (image.isNull()) {
        QPixmap px(120, 120);
        px.fill(Qt::darkGray);
        return px;
    }
    return QPixmap::fromImage(
        image.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace

BrowseTab::BrowseTab(QWidget *parent) : QWidget(parent) {
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_sourceList = new QListWidget(splitter);
    m_sourceList->setMinimumWidth(220);
    m_sourceList->setMaximumWidth(320);

    auto *rightPane = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_grid = new QListWidget(rightPane);
    m_grid->setViewMode(QListWidget::IconMode);
    m_grid->setIconSize(QSize(120, 120));
    m_grid->setGridSize(QSize(140, 150));
    m_grid->setResizeMode(QListWidget::Adjust);
    m_grid->setMovement(QListWidget::Static);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setSpacing(4);

    auto *buttonRow = new QHBoxLayout;
    m_browseButton = new QPushButton("Browse Folder…", rightPane);
    m_openButton = new QPushButton("Open in Retouch", rightPane);
    m_openButton->setEnabled(false);
    buttonRow->addWidget(m_browseButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_openButton);

    rightLayout->addWidget(m_grid, 1);
    rightLayout->addLayout(buttonRow);

    splitter->addWidget(m_sourceList);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(1, 1);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(splitter);

    connect(m_sourceList, &QListWidget::itemActivated, this, &BrowseTab::onSourceActivated);
    connect(m_grid, &QListWidget::itemActivated, this, &BrowseTab::onGridActivated);
    connect(m_grid, &QListWidget::itemSelectionChanged, this, [this] {
        m_openButton->setEnabled(!m_grid->selectedItems().isEmpty());
    });
    connect(m_openButton, &QPushButton::clicked, this, &BrowseTab::onOpenClicked);
    connect(m_browseButton, &QPushButton::clicked, this, &BrowseTab::browseForFolder);

    populateSourceList();
}

void BrowseTab::refresh() {
    populateSourceList();
}

void BrowseTab::populateSourceList() {
    m_sourceList->clear();

    auto addHeader = [this](const QString &text) {
        auto *item = new QListWidgetItem(text, m_sourceList);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    };

    // Dated session folders already on disk under the tether base directory.
    SessionManager sm;
    const QDir base(sm.baseDirectory());
    QStringList sessionDirs;
    if (base.exists()) {
        const QFileInfoList entries =
            base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QFileInfo &fi : entries)
            sessionDirs << fi.absoluteFilePath();
    }
    // Recently opened sessions may live outside the base dir; fold them in.
    for (const QString &dir : RecentSessions::load())
        if (!sessionDirs.contains(dir))
            sessionDirs << dir;

    if (!sessionDirs.isEmpty()) {
        addHeader("Sessions");
        for (const QString &dir : sessionDirs) {
            auto *item = new QListWidgetItem(QFileInfo(dir).fileName(), m_sourceList);
            item->setData(kPathRole, dir);
            item->setData(kKindRole, SessionKind);
            item->setToolTip(dir);
        }
    }

    const QStringList projects = RecentProjects::load();
    if (!projects.isEmpty()) {
        addHeader("Projects");
        for (const QString &path : projects) {
            auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_sourceList);
            item->setData(kPathRole, path);
            item->setData(kKindRole, ProjectKind);
            item->setToolTip(path);
        }
    }
}

void BrowseTab::onSourceActivated(QListWidgetItem *item) {
    if (!item) return;
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty()) return;

    if (item->data(kKindRole).toInt() == ProjectKind) {
        emit openRequested({path});
        return;
    }
    loadFolder(path);
}

void BrowseTab::loadFolder(const QString &dir) {
    m_currentDir = dir;
    m_grid->clear();

    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (!isBrowsableImage(fi)) continue;
        const QString path = fi.absoluteFilePath();

        // Same cheap-thumbnail-first strategy as the Retouch filmstrip: a
        // cached edited thumbnail, else the RAW's embedded JPEG preview,
        // else (for plain JPEG/PNG/TIFF) a scaled decode of the file itself.
        QImage thumb = EditSidecar::loadThumbnail(path);
        if (thumb.isNull()) thumb = NefPreview::extract(path);
        if (thumb.isNull()) thumb = QImage(path);

        auto *item = new QListWidgetItem(iconFor(thumb), fi.fileName(), m_grid);
        item->setData(kPathRole, path);
        item->setToolTip(fi.fileName());
    }
}

void BrowseTab::onGridActivated(QListWidgetItem *item) {
    if (!item) return;
    emit openRequested({item->data(kPathRole).toString()});
}

void BrowseTab::onOpenClicked() {
    QStringList paths;
    for (QListWidgetItem *item : m_grid->selectedItems())
        paths << item->data(kPathRole).toString();
    if (!paths.isEmpty())
        emit openRequested(paths);
}

void BrowseTab::browseForFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Browse folder", QDir(QDir::homePath()).filePath("Pictures/Tether"));
    if (dir.isEmpty()) return;
    loadFolder(dir);
}
