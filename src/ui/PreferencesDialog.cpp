#include "ui/PreferencesDialog.h"

#include "camera/CameraModels.h"

#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QListWidget>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QFont>

void afFrameForModel(const QString &id, int &w, int &h) {
    QSettings s;
    const cammodel::Model *m = cammodel::byId(id.toStdString());
    int dw = m ? m->afFrameW : 640;
    int dh = m ? m->afFrameH : 426;
    w = s.value(QString("af/models/%1/frameWidth").arg(id), dw).toInt();
    h = s.value(QString("af/models/%1/frameHeight").arg(id), dh).toInt();
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    resize(560, 420);

    auto *outer = new QVBoxLayout(this);

    auto *row = new QHBoxLayout;
    outer->addLayout(row, /*stretch=*/1);

    auto *nav = new QListWidget;
    nav->setFixedWidth(150);
    nav->addItem("General");
    nav->addItem("Keyboard Shortcuts");
    row->addWidget(nav);

    auto *pages = new QStackedWidget;
    pages->addWidget(buildGeneralPage());
    pages->addWidget(buildShortcutsPage());
    row->addWidget(pages, /*stretch=*/1);

    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    nav->setCurrentRow(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(buttons);

    // Restore the last-used model.
    QSettings s;
    QString cur = s.value("af/currentModel", "custom").toString();
    int idx = m_model->findData(cur);
    if (idx < 0) idx = m_model->findData("custom");
    {
        QSignalBlocker b(m_model);
        m_model->setCurrentIndex(idx);
    }
    loadFrameForCurrentModel();

    connect(m_model, &QComboBox::currentIndexChanged, this,
            [this](int) { onModelChanged(); });
    connect(m_frameW, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
    connect(m_frameH, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
}

QWidget *PreferencesDialog::buildGeneralPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_model = new QComboBox;
    for (const cammodel::Model &m : cammodel::models())
        m_model->addItem(m.display, QString::fromLatin1(m.id));
    form->addRow("Camera model:", m_model);

    m_frameW = new QSpinBox;
    m_frameH = new QSpinBox;
    m_frameW->setRange(1, 20000);
    m_frameH->setRange(1, 20000);
    form->addRow("AF frame width:", m_frameW);
    form->addRow("AF frame height:", m_frameH);

    m_calibrate = new QPushButton("Calibrate…");
    form->addRow(QString(), m_calibrate);
    connect(m_calibrate, &QPushButton::clicked, this,
            [this] { emit calibrationRequested(); });

    auto *hint = new QLabel(
        "Click-to-focus calibration. Click Calibrate…, then click the point "
        "you want in focus, then click where it actually snapped sharp -- "
        "the AF frame size is solved automatically from those two clicks. "
        "Values are remembered per model.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(1);
    return page;
}

namespace {
// One keyboard shortcut reference entry: display name + key text. Grouped
// under a category heading in the Keyboard Shortcuts page. Reference-only —
// this mirrors the shortcuts wired via setShortcut()/QShortcut in
// RetouchWindow.cpp and TetherView.cpp; it doesn't rebind anything.
struct ShortcutEntry {
    const char *action;
    const char *keys;
};
struct ShortcutGroup {
    const char *heading;
    std::initializer_list<ShortcutEntry> entries;
};
constexpr ShortcutGroup kShortcutGroups[] = {
    {"Menu", {
        {"New…", "Ctrl+N"},
        {"Save", "Ctrl+S"},
        {"Undo", "Ctrl+Z"},
        {"Redo", "Ctrl+Y"},
        {"Copy Edits", "Ctrl+Shift+C"},
        {"Paste Edits", "Ctrl+Shift+V"},
        {"Sync Edits to Selected", "Ctrl+Shift+S"},
        {"Group Shapes", "Ctrl+G"},
        {"Ungroup Shapes", "Ctrl+Shift+G"},
        {"Preferences…", "Ctrl+,"},
    }},
    {"Tools", {
        {"Zoom", "Z"},
        {"Crop", "C"},
        {"Spot Heal", "H"},
        {"Brush", "B"},
        {"Erase", "E"},
        {"Remove Object", "J"},
        {"Text", "T"},
        {"Shape", "U"},
        {"Local Masks", "K"},
        {"Erase instead of paint (while Brush is active)", "Alt + drag"},
        {"Resize brush (while Brush/Erase is active)", "Ctrl + scroll wheel"},
    }},
    {"Canvas", {
        {"Deselect all tools", "Esc"},
        {"Fit to window", "Ctrl+0"},
        {"Swap foreground/background color", "X"},
        {"Reset colors", "D"},
        {"Fill with background color", "Ctrl+Backspace"},
        {"Fill with foreground color", "Alt+Backspace"},
        {"Capture (tethering)", "Space"},
    }},
};
} // namespace

QWidget *PreferencesDialog::buildShortcutsPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *tree = new QTreeWidget;
    tree->setColumnCount(2);
    tree->setHeaderLabels({"Action", "Shortcut"});
    tree->setRootIsDecorated(true);
    tree->setAlternatingRowColors(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    for (const ShortcutGroup &group : kShortcutGroups) {
        auto *heading = new QTreeWidgetItem(tree, {group.heading});
        QFont f = heading->font(0);
        f.setBold(true);
        heading->setFont(0, f);
        heading->setFlags(heading->flags() & ~Qt::ItemIsSelectable);
        for (const ShortcutEntry &e : group.entries)
            new QTreeWidgetItem(heading, {e.action, e.keys});
        heading->setExpanded(true);
    }
    layout->addWidget(tree);
    return page;
}

QString PreferencesDialog::currentModelId() const {
    return m_model->currentData().toString();
}

void PreferencesDialog::loadFrameForCurrentModel() {
    int w = 0, h = 0;
    afFrameForModel(currentModelId(), w, h);
    QSignalBlocker bw(m_frameW);
    QSignalBlocker bh(m_frameH);
    m_frameW->setValue(w);
    m_frameH->setValue(h);
}

void PreferencesDialog::onModelChanged() {
    QSettings s;
    s.setValue("af/currentModel", currentModelId());
    loadFrameForCurrentModel();
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::onFrameEdited() {
    QSettings s;
    const QString id = currentModelId();
    s.setValue(QString("af/models/%1/frameWidth").arg(id), m_frameW->value());
    s.setValue(QString("af/models/%1/frameHeight").arg(id), m_frameH->value());
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::selectModelById(const QString &id) {
    if (id.isEmpty()) return;
    int idx = m_model->findData(id);
    if (idx < 0 || idx == m_model->currentIndex()) return;
    m_model->setCurrentIndex(idx); // triggers onModelChanged()
}

void PreferencesDialog::setAfFrame(int w, int h) {
    {
        QSignalBlocker bw(m_frameW);
        QSignalBlocker bh(m_frameH);
        m_frameW->setValue(w);
        m_frameH->setValue(h);
    }
    onFrameEdited(); // persist for the current model + emit afFrameSizeChanged
}
