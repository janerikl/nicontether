#include "ui/AssetsPanel.h"

#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

AssetsPanel::AssetsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(64, 64));
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(6);
    layout->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout;
    m_insert = new QPushButton("Insert", this);
    m_insert->setEnabled(false);
    m_delete = new QPushButton("Delete", this);
    m_delete->setEnabled(false);
    buttons->addWidget(m_insert);
    buttons->addWidget(m_delete);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool has = row >= 0 && row < m_assets.size();
        m_insert->setEnabled(has);
        m_delete->setEnabled(has);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const int row = m_list->row(item);
        if (row >= 0 && row < m_assets.size()) emit insertAssetRequested(m_assets[row]);
    });
    connect(m_insert, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row >= 0 && row < m_assets.size()) emit insertAssetRequested(m_assets[row]);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row >= 0 && row < m_assets.size()) emit deleteAssetRequested(m_assets[row].name);
    });
}

void AssetsPanel::setAssets(const QList<AssetStamp> &assets) {
    m_assets = assets;
    rebuildList();
}

void AssetsPanel::rebuildList() {
    m_list->clear();
    for (const AssetStamp &asset : m_assets) {
        auto *item = new QListWidgetItem(QIcon(asset.imagePath), asset.name);
        m_list->addItem(item);
    }
    m_insert->setEnabled(false);
    m_delete->setEnabled(false);
}
