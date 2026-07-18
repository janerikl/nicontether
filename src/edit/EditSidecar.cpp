#include "edit/EditSidecar.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace EditSidecar {

QString pathFor(const QString &imagePath) {
    return imagePath + ".nte.json";
}

bool exists(const QString &imagePath) {
    return QFileInfo::exists(pathFor(imagePath));
}

bool save(const QString &imagePath, const Adjustments &a) {
    QJsonObject o;
    o["version"] = 1;
    o["brightness"] = a.brightness;
    o["contrast"] = a.contrast;
    o["highlights"] = a.highlights;
    o["shadows"] = a.shadows;
    o["saturation"] = a.saturation;
    o["vibrance"] = a.vibrance;
    o["temperature"] = a.temperature;
    o["tint"] = a.tint;
    o["wbR"] = a.wbR;
    o["wbG"] = a.wbG;
    o["wbB"] = a.wbB;
    o["clarity"] = a.clarity;
    o["sharpen"] = a.sharpen;
    o["vignette"] = a.vignette;
    o["rotationQuadrants"] = a.rotationQuadrants;
    o["flipH"] = a.flipH;
    o["flipV"] = a.flipV;
    if (!a.cropRect.isNull()) {
        QJsonObject c;
        c["x"] = a.cropRect.x();
        c["y"] = a.cropRect.y();
        c["w"] = a.cropRect.width();
        c["h"] = a.cropRect.height();
        o["crop"] = c;
    }
    QJsonArray curve;
    for (const QPointF &p : a.curve)
        curve.append(QJsonArray{p.x(), p.y()});
    o["curve"] = curve;

    QFile f(pathFor(imagePath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool load(const QString &imagePath, Adjustments &out) {
    QFile f(pathFor(imagePath));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    QJsonObject o = doc.object();

    Adjustments a;
    a.brightness = o["brightness"].toInt();
    a.contrast = o["contrast"].toInt();
    a.highlights = o["highlights"].toInt();
    a.shadows = o["shadows"].toInt();
    a.saturation = o["saturation"].toInt();
    a.vibrance = o["vibrance"].toInt();
    a.temperature = o["temperature"].toInt();
    a.tint = o["tint"].toInt();
    a.wbR = o["wbR"].toDouble(1.0);
    a.wbG = o["wbG"].toDouble(1.0);
    a.wbB = o["wbB"].toDouble(1.0);
    a.clarity = o["clarity"].toInt();
    a.sharpen = o["sharpen"].toInt();
    a.vignette = o["vignette"].toInt();
    a.rotationQuadrants = o["rotationQuadrants"].toInt();
    a.flipH = o["flipH"].toBool();
    a.flipV = o["flipV"].toBool();
    if (o.contains("crop")) {
        QJsonObject c = o["crop"].toObject();
        a.cropRect = QRect(c["x"].toInt(), c["y"].toInt(),
                           c["w"].toInt(), c["h"].toInt());
    }
    for (const QJsonValue &v : o["curve"].toArray()) {
        QJsonArray p = v.toArray();
        if (p.size() == 2) a.curve.append(QPointF(p[0].toDouble(), p[1].toDouble()));
    }
    out = a;
    return true;
}

} // namespace EditSidecar
