#pragma once

#include <QWidget>

#include "edit/AssetStamp.h"

class QListWidget;
class QPushButton;

// Dockable library of saved AssetStamps (objects cut out of a photo via
// Select > Save Selection as Asset). Purely a view -- RetouchWindow owns the
// AssetStampStore and pushes its contents in via setAssets(); this panel just
// shows thumbnails and emits intent signals.
class AssetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AssetsPanel(QWidget *parent = nullptr);

    void setAssets(const QList<AssetStamp> &assets);

signals:
    void insertAssetRequested(const AssetStamp &asset);
    void deleteAssetRequested(const QString &name);

private:
    void rebuildList();

    QList<AssetStamp> m_assets;
    QListWidget *m_list = nullptr;
    QPushButton *m_insert = nullptr;
    QPushButton *m_delete = nullptr;
};
