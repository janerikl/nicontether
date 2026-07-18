#include "AppIcon.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRadialGradient>
#include <QtMath>

namespace {

const QColor kGraphite(48, 54, 62);
const QColor kSteel(70, 110, 150);

void paintShutter(QPixmap &pm) {
    const qreal S = pm.width();
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c(S / 2.0, S / 2.0);
    const qreal R = S * 0.46;   // visible disc radius
    const qreal Ro = S * 0.60;  // blade outer reach (clipped to R)
    const qreal Ri = S * 0.17;  // central opening radius
    const int blades = 6;

    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p.setClipPath(disc);

    for (int i = 0; i < blades; ++i) {
        const qreal a0 = qDegreesToRadians(360.0 * i / blades);
        const qreal a1 = qDegreesToRadians(360.0 * (i + 1) / blades);

        const QPointF outerA(c.x() + Ro * qCos(a0), c.y() + Ro * qSin(a0));
        const QPointF outerB(c.x() + Ro * qCos(a1), c.y() + Ro * qSin(a1));
        const QPointF innerB(c.x() + Ri * qCos(a1), c.y() + Ri * qSin(a1));

        QPolygonF blade;
        blade << outerA << outerB << innerB;

        QLinearGradient g(outerA, innerB);
        g.setColorAt(0.0, kGraphite);
        g.setColorAt(1.0, kSteel);

        p.setPen(QPen(kGraphite.darker(140), qMax(1.0, S / 128.0)));
        p.setBrush(g);
        p.drawPolygon(blade);
    }

    // Steel glow in the opening.
    QRadialGradient rg(c, Ri * 1.3);
    rg.setColorAt(0.0, QColor(90, 140, 190, 90));
    rg.setColorAt(1.0, QColor(90, 140, 190, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(rg);
    p.drawEllipse(c, Ri * 1.3, Ri * 1.3);

    // Rim ring for definition.
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kSteel.darker(120), qMax(1.0, S / 64.0)));
    p.drawEllipse(c, R, R);

    p.end();
}

}  // namespace

QIcon makeShutterIcon() {
    QIcon icon;
    for (int sz : {16, 32, 64, 128, 256}) {
        QPixmap pm(sz, sz);
        paintShutter(pm);
        icon.addPixmap(pm);
    }
    return icon;
}
