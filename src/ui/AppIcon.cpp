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
    const qreal Ri = S * 0.21;  // central hexagonal opening radius
    const int blades = 6;
    const qreal twist = qDegreesToRadians(40.0);  // pinwheel sweep of the blades

    // Hexagon vertices of the opening (shared by the seams and the punch-out).
    auto hexVertex = [&](int i) {
        const qreal a = qDegreesToRadians(60.0 * i + 30.0);
        return QPointF(c.x() + Ri * qCos(a), c.y() + Ri * qSin(a));
    };

    // 1) Iris body: a graphite-to-steel disc.
    QPainterPath disc;
    disc.addEllipse(c, R, R);
    QRadialGradient body(c, R);
    body.setColorAt(0.0, kSteel.lighter(118));
    body.setColorAt(0.62, kSteel);
    body.setColorAt(1.0, kGraphite);
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawPath(disc);

    // 2) Blade seams: from each hexagon vertex, swept out to the rim. These
    //    diagonal seams are what make the disc read as an overlapping-blade
    //    camera shutter rather than a plain wheel.
    p.setClipPath(disc);
    QPen seam(kGraphite.darker(155), qMax(1.0, S / 38.0));
    seam.setCapStyle(Qt::RoundCap);
    p.setBrush(Qt::NoBrush);
    p.setPen(seam);
    for (int i = 0; i < blades; ++i) {
        const qreal a = qDegreesToRadians(60.0 * i + 30.0);
        const QPointF outer(c.x() + R * 1.15 * qCos(a - twist),
                            c.y() + R * 1.15 * qSin(a - twist));
        p.drawLine(hexVertex(i), outer);
    }
    p.setClipping(false);

    // 3) Punch the hexagonal opening out to transparent.
    QPolygonF hex;
    for (int i = 0; i < blades; ++i)
        hex << hexVertex(i);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawPolygon(hex);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // 4) Bright edge around the opening.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kSteel.lighter(145), qMax(1.0, S / 64.0)));
    p.drawPolygon(hex);

    // 5) Outer rim for definition.
    p.setPen(QPen(kGraphite.darker(135), qMax(1.0, S / 40.0)));
    p.drawEllipse(c, R - S / 80.0, R - S / 80.0);

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
