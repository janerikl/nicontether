#include "ui/BrowseTab.h"

#include "capture/NefPreview.h"
#include "capture/SessionManager.h"
#include "device/MtpController.h"
#include "edit/EditSidecar.h"
#include "edit/RawMetadata.h"
#include "edit/RecentProjects.h"
#include "edit/RecentSessions.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLocale>
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

bool isRawFile(const QFileInfo &fi) {
    static const QSet<QString> kRawExt = {
        "nef", "cr2", "cr3", "arw", "dng", "raf", "rw2", "orf"};
    return kRawExt.contains(fi.suffix().toLower());
}

// Role used to stash the absolute file path on both list widgets' items.
constexpr int kPathRole = Qt::UserRole;
// Source-list item kinds: a session folder (loadFolder on activation) vs. a
// project file (opens directly, since a .ploom is a single edit not a
// folder of photos).
constexpr int kKindRole = Qt::UserRole + 1;
enum Kind { SessionKind, ProjectKind, DeviceKind };
// Grid-item role used only for device (MTP) entries: the libmtp object id,
// used to match async thumbnails and to build the import list.
constexpr int kIdRole = Qt::UserRole + 2;

int countBrowsableImages(const QString &dir) {
    int count = 0;
    for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files))
        if (isBrowsableImage(fi)) ++count;
    return count;
}

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

    auto *previewPane = new QWidget(splitter);
    previewPane->setMinimumWidth(320);
    auto *previewLayout = new QVBoxLayout(previewPane);

    m_previewImage = new QLabel(previewPane);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(480);
    m_previewImage->setStyleSheet("background: palette(dark);");

    m_previewMeta = new QLabel(previewPane);
    m_previewMeta->setWordWrap(true);
    m_previewMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewMeta->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    previewLayout->addWidget(m_previewImage);
    previewLayout->addWidget(m_previewMeta);
    previewLayout->addStretch(1);

    splitter->addWidget(m_sourceList);
    splitter->addWidget(rightPane);
    splitter->addWidget(previewPane);
    splitter->setStretchFactor(0, 0);
    // Grid and preview split the remaining width evenly, so the preview
    // takes roughly half of the browse area.
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({240, 1, 1});

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(splitter);

    connect(m_sourceList, &QListWidget::itemActivated, this, &BrowseTab::onSourceActivated);
    // A session's thumbnails load as soon as it becomes current -- via a
    // click or Up/Down arrow navigation -- not only on itemActivated (default
    // double-click/Enter). Projects still only open on itemActivated, since
    // that's a more deliberate action than just moving the current-item
    // highlight.
    connect(m_sourceList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item && item->data(kKindRole).toInt() == SessionKind) onSourceActivated(item);
            });
    connect(m_grid, &QListWidget::itemActivated, this, &BrowseTab::onGridActivated);
    connect(m_grid, &QListWidget::itemSelectionChanged, this, [this] {
        m_openButton->setEnabled(!m_grid->selectedItems().isEmpty());
    });
    connect(m_grid, &QListWidget::itemSelectionChanged, this, &BrowseTab::onGridSelectionChanged);
    connect(m_openButton, &QPushButton::clicked, this, &BrowseTab::onOpenClicked);
    connect(m_browseButton, &QPushButton::clicked, this, &BrowseTab::browseForFolder);

    m_mtpController = new MtpController(this);
    connect(m_mtpController, &MtpController::deviceConnected, this, &BrowseTab::onDeviceConnected);
    connect(m_mtpController, &MtpController::deviceDisconnected, this, &BrowseTab::onDeviceDisconnected);
    connect(m_mtpController, &MtpController::filesListed, this, &BrowseTab::onFilesListed);
    connect(m_mtpController, &MtpController::thumbnailReady, this, &BrowseTab::onThumbnailReady);
    connect(m_mtpController, &MtpController::importComplete, this, &BrowseTab::onImportComplete);

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

    if (!m_deviceName.isEmpty()) {
        addHeader("Devices");
        auto *item = new QListWidgetItem(m_deviceName, m_sourceList);
        item->setData(kKindRole, DeviceKind);
        item->setToolTip("Browse photos on " + m_deviceName);
    }

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
            const int count = countBrowsableImages(dir);
            auto *item = new QListWidgetItem(m_sourceList);
            item->setData(kPathRole, dir);
            item->setData(kKindRole, SessionKind);
            item->setToolTip(dir);

            auto *row = new QWidget;
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 0, 4, 0);
            auto *nameLabel = new QLabel(QFileInfo(dir).fileName(), row);
            auto *countLabel = new QLabel(QString::number(count), row);
            countLabel->setStyleSheet("color: gray;");
            rowLayout->addWidget(nameLabel, 1);
            rowLayout->addWidget(countLabel, 0, Qt::AlignRight);
            item->setSizeHint(row->sizeHint());
            m_sourceList->setItemWidget(item, row);
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

    const int kind = item->data(kKindRole).toInt();
    m_deviceSelected = (kind == DeviceKind);
    m_openButton->setText(m_deviceSelected ? "Import Selected" : "Open in Retouch");
    m_openButton->setEnabled(false);

    if (m_deviceSelected) {
        m_currentDir.clear();
        loadDeviceGrid();
        return;
    }

    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty()) return;

    if (kind == ProjectKind) {
        emit openRequested({path});
        return;
    }
    loadFolder(path);
}

void BrowseTab::loadDeviceGrid() {
    m_grid->clear();
    m_previewImage->clear();
    m_previewMeta->clear();
    m_deviceEntries.clear();
    auto *loading = new QListWidgetItem("Loading…", m_grid);
    loading->setFlags(loading->flags() & ~Qt::ItemIsSelectable);
    m_mtpController->refreshFiles();
}

void BrowseTab::loadFolder(const QString &dir) {
    m_currentDir = dir;
    m_grid->clear();
    m_previewImage->clear();
    m_previewMeta->clear();

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
    if (m_deviceSelected) return; // double-click has no direct-open meaning for device items
    emit openRequested({item->data(kPathRole).toString()});
}

void BrowseTab::onGridSelectionChanged() {
    if (m_deviceSelected) {
        // Device photos aren't local files yet, so there's nothing to preview
        // or read metadata from until they're imported.
        m_previewImage->clear();
        m_previewMeta->clear();
        return;
    }

    const QList<QListWidgetItem *> selected = m_grid->selectedItems();
    if (selected.size() != 1) {
        m_previewImage->clear();
        m_previewMeta->clear();
        return;
    }

    const QString path = selected.first()->data(kPathRole).toString();
    const QFileInfo fi(path);

    // Same cheap-thumbnail-first strategy as loadFolder's grid icons, just
    // decoded at a larger size for the preview pane.
    QImage img = EditSidecar::loadThumbnail(path);
    if (img.isNull()) img = NefPreview::extract(path);
    if (img.isNull()) img = QImage(path);

    if (img.isNull()) {
        m_previewImage->clear();
    } else {
        const QSize target = m_previewImage->size().expandedTo(QSize(320, 480));
        m_previewImage->setPixmap(QPixmap::fromImage(img).scaled(
            target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    const QLocale locale;
    QString meta = QString("<b>%1</b><br>").arg(fi.fileName());

    const RawMetadata::Info raw = isRawFile(fi) ? RawMetadata::read(path) : RawMetadata::Info();
    if (raw.valid) {
        if (raw.width > 0 && raw.height > 0)
            meta += QString("%1 × %2 px<br>").arg(raw.width).arg(raw.height);
        const QString camera = QStringList{raw.make, raw.model}.join(' ').trimmed();
        if (!camera.isEmpty()) meta += camera + "<br>";
        if (!raw.lens.isEmpty()) meta += raw.lens + "<br>";
        if (raw.isoSpeed > 0) meta += QString("ISO %1<br>").arg(int(raw.isoSpeed));
        if (raw.aperture > 0) meta += QString("f/%1<br>").arg(raw.aperture, 0, 'g', 3);
        if (raw.shutterSpeed > 0) {
            meta += raw.shutterSpeed < 1
                        ? QString("1/%1 s<br>").arg(qRound(1.0 / raw.shutterSpeed))
                        : QString("%1 s<br>").arg(raw.shutterSpeed, 0, 'g', 3);
        }
        if (raw.focalLength > 0) meta += QString("%1 mm<br>").arg(int(raw.focalLength));
        if (raw.timestamp.isValid())
            meta += QString("Taken: %1<br>").arg(raw.timestamp.toString("yyyy-MM-dd hh:mm"));
    } else if (!img.isNull()) {
        meta += QString("%1 × %2 px<br>").arg(img.width()).arg(img.height());
    }

    meta += QString("%1<br>").arg(locale.formattedDataSize(fi.size()));
    meta += QString("Modified: %1")
                .arg(fi.lastModified().toString("yyyy-MM-dd hh:mm"));
    m_previewMeta->setText(meta);
}

void BrowseTab::onOpenClicked() {
    if (m_deviceSelected) {
        onImportClicked();
        return;
    }
    QStringList paths;
    for (QListWidgetItem *item : m_grid->selectedItems())
        paths << item->data(kPathRole).toString();
    if (!paths.isEmpty())
        emit openRequested(paths);
}

void BrowseTab::onImportClicked() {
    QVector<MtpEntry> toImport;
    for (QListWidgetItem *item : m_grid->selectedItems()) {
        const quint32 id = item->data(kIdRole).toUInt();
        for (const MtpEntry &e : m_deviceEntries) {
            if (e.id == id) { toImport << e; break; }
        }
    }
    if (toImport.isEmpty()) return;

    m_openButton->setEnabled(false);
    m_openButton->setText("Importing…");

    SessionManager sm;
    const QString destDir = sm.startSession("Android_Import");
    m_mtpController->importFiles(toImport, destDir);
}

void BrowseTab::onDeviceConnected(const QString &name) {
    m_deviceName = name;
    populateSourceList();
}

void BrowseTab::onDeviceDisconnected() {
    m_deviceName.clear();
    if (m_deviceSelected) {
        m_deviceSelected = false;
        m_grid->clear();
        m_previewImage->clear();
        m_previewMeta->clear();
        m_openButton->setText("Open in Retouch");
        m_openButton->setEnabled(false);
    }
    populateSourceList();
}

void BrowseTab::onFilesListed(const QVector<MtpEntry> &entries) {
    if (!m_deviceSelected) return; // arrived after the user switched sources
    m_deviceEntries = entries;
    m_grid->clear();

    QPixmap placeholder(120, 120);
    placeholder.fill(Qt::darkGray);

    for (const MtpEntry &e : entries) {
        auto *item = new QListWidgetItem(placeholder, e.name, m_grid);
        item->setData(kIdRole, e.id);
        item->setToolTip(e.name);
    }
}

void BrowseTab::onThumbnailReady(quint32 id, const QImage &image) {
    if (!m_deviceSelected || image.isNull()) return;
    for (int i = 0; i < m_grid->count(); ++i) {
        QListWidgetItem *item = m_grid->item(i);
        if (item->data(kIdRole).toUInt() == id) {
            item->setIcon(QIcon(QPixmap::fromImage(
                image.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
            break;
        }
    }
}

void BrowseTab::onImportComplete(const QStringList &savedPaths) {
    m_openButton->setText("Import Selected");
    populateSourceList();
    if (!savedPaths.isEmpty())
        loadFolder(QFileInfo(savedPaths.first()).absolutePath());
}

void BrowseTab::browseForFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Browse folder", QDir(QDir::homePath()).filePath("Pictures/Tether"));
    if (dir.isEmpty()) return;
    loadFolder(dir);
}
