#include "ui/ToolFlyout.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>

namespace {
constexpr int kHoldMs = 350; // press-and-hold threshold for opening the flyout
const QColor kGlyph(235, 235, 235);
} // namespace

ToolFlyout::ToolFlyout(const QVector<SubTool> &tools, int activeId,
                       QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(
        "ToolFlyout { background: #2b2f36; border: 1px solid #454b54;"
        "             border-radius: 6px; }"
        "QToolButton { border: none; padding: 5px; border-radius: 4px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.10); }"
        "QToolButton[active=\"true\"] { background: #3a3f47; }");

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(2);

    for (const SubTool &t : tools) {
        auto *b = new QToolButton(this);
        b->setIconSize(QSize(22, 22));
        b->setIcon(QIcon(t.draw(kGlyph)));
        b->setToolTip(t.tooltip);
        b->setProperty("active", t.id == activeId);
        const int id = t.id;
        connect(b, &QToolButton::clicked, this, [this, id] {
            emit chosen(id);
            close();
        });
        row->addWidget(b);
    }
}

void ToolFlyout::showAt(const QPoint &globalTopLeft) {
    adjustSize();
    move(globalTopLeft);
    show();
}

FlyoutToolButton::FlyoutToolButton(QWidget *parent) : QToolButton(parent) {
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(kHoldMs);
    connect(m_holdTimer, &QTimer::timeout, this, [this] {
        m_flyoutShown = true;
        setDown(false); // clear the pressed look; this is a hold, not a click
        emit flyoutRequested();
    });
}

void FlyoutToolButton::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_flyoutShown = false;
        m_holdTimer->start();
    }
    QToolButton::mousePressEvent(e);
}

void FlyoutToolButton::mouseReleaseEvent(QMouseEvent *e) {
    m_holdTimer->stop();
    if (m_flyoutShown) {
        // The hold already opened the flyout; swallow the release so the tool
        // does not also toggle.
        m_flyoutShown = false;
        e->accept();
        return;
    }
    QToolButton::mouseReleaseEvent(e);
}
