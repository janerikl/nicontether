#pragma once

#include <QToolButton>
#include <QVector>
#include <QWidget>

class QTimer;

// A subtool a flyout tool can offer. `draw` paints a programmatic glyph in the
// requested colour, matching the sidebar tool-icon style (no image assets).
struct SubTool {
    int id;
    QPixmap (*draw)(const QColor &);
    QString label;
    QString tooltip;
};

// Photoshop-style flyout: a small frameless popup showing a horizontal strip of
// subtool icons next to the owning tool button. Emits chosen(id) on pick and
// closes itself; clicking away also closes it (Qt::Popup semantics).
class ToolFlyout : public QWidget {
    Q_OBJECT
public:
    ToolFlyout(const QVector<SubTool> &tools, int activeId,
               QWidget *parent = nullptr);

    // Pop up with the strip's top-left at the given global position.
    void showAt(const QPoint &globalTopLeft);

signals:
    void chosen(int id);
};

// A QToolButton that distinguishes a quick click (normal toggle) from a
// press-and-hold. Holding past the threshold emits flyoutRequested() and
// suppresses the toggle, so click = use tool, hold = open the subtool flyout.
class FlyoutToolButton : public QToolButton {
    Q_OBJECT
public:
    explicit FlyoutToolButton(QWidget *parent = nullptr);

signals:
    void flyoutRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QTimer *m_holdTimer = nullptr;
    bool m_flyoutShown = false;
};
