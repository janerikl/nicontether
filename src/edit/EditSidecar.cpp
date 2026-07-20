#include "edit/EditSidecar.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

QJsonObject levelsChannelToJson(const LevelsChannel &c) {
    QJsonObject j;
    j["inBlack"] = c.inBlack;
    j["inWhite"] = c.inWhite;
    j["gamma"] = c.gamma;
    j["outBlack"] = c.outBlack;
    j["outWhite"] = c.outWhite;
    return j;
}

LevelsChannel levelsChannelFromJson(const QJsonObject &j) {
    LevelsChannel c;
    c.inBlack = j["inBlack"].toInt(0);
    c.inWhite = j["inWhite"].toInt(255);
    c.gamma = j["gamma"].toDouble(1.0);
    c.outBlack = j["outBlack"].toInt(0);
    c.outWhite = j["outWhite"].toInt(255);
    return c;
}

QJsonObject levelsToJson(const Levels &lv) {
    QJsonObject j;
    j["rgb"] = levelsChannelToJson(lv.rgb);
    j["r"] = levelsChannelToJson(lv.r);
    j["g"] = levelsChannelToJson(lv.g);
    j["b"] = levelsChannelToJson(lv.b);
    return j;
}

Levels levelsFromJson(const QJsonObject &j) {
    Levels lv;
    lv.rgb = levelsChannelFromJson(j["rgb"].toObject());
    lv.r = levelsChannelFromJson(j["r"].toObject());
    lv.g = levelsChannelFromJson(j["g"].toObject());
    lv.b = levelsChannelFromJson(j["b"].toObject());
    return lv;
}

QJsonArray curveToJson(const QVector<QPointF> &curve) {
    QJsonArray a;
    for (const QPointF &p : curve) a.append(QJsonArray{p.x(), p.y()});
    return a;
}

QVector<QPointF> curveFromJson(const QJsonArray &a) {
    QVector<QPointF> curve;
    for (const QJsonValue &v : a) {
        QJsonArray p = v.toArray();
        if (p.size() == 2) curve.append(QPointF(p[0].toDouble(), p[1].toDouble()));
    }
    return curve;
}

} // namespace

namespace EditSidecar {

QString pathFor(const QString &imagePath) {
    return imagePath + ".nte.json";
}

bool exists(const QString &imagePath) {
    return QFileInfo::exists(pathFor(imagePath));
}

bool save(const QString &imagePath, const Adjustments &a) {
    QJsonObject o;
    o["version"] = 5;
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
    o["denoise"] = a.denoise;
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
    o["curve"] = curveToJson(a.curve);
    if (!a.levels.isIdentity()) o["levels"] = levelsToJson(a.levels);
    if (!a.colorRanges.isEmpty()) {
        QJsonArray ranges;
        for (const ColorRangeAdjust &cr : a.colorRanges) {
            QJsonObject j;
            j["r"] = cr.r;
            j["g"] = cr.g;
            j["b"] = cr.b;
            j["ch"] = cr.channel;
            j["amt"] = cr.amount;
            ranges.append(j);
        }
        o["colorRanges"] = ranges;
    }

    if (!a.masks.isEmpty()) {
        QJsonArray masks;
        for (const Mask &m : a.masks) {
            QJsonObject j;
            j["name"] = m.name;
            j["visible"] = m.visible;
            j["opacity"] = m.opacity;
            j["blend"] = int(m.blend);
            j["sourceImagePath"] = m.sourceImagePath;
            j["sourceImageOffsetX"] = m.sourceImageOffset.x();
            j["sourceImageOffsetY"] = m.sourceImageOffset.y();
            j["sourceImageScaleX"] = m.sourceImageScale.x();
            j["sourceImageScaleY"] = m.sourceImageScale.y();
            j["sourceImageLockRatio"] = m.sourceImageLockRatio;
            j["type"] = int(m.type);
            j["inverted"] = m.inverted;
            j["feather"] = m.feather;
            j["cx"] = m.center.x();
            j["cy"] = m.center.y();
            j["rx"] = m.radiusX;
            j["ry"] = m.radiusY;
            j["angle"] = m.angle;
            j["p0x"] = m.p0.x();
            j["p0y"] = m.p0.y();
            j["p1x"] = m.p1.x();
            j["p1y"] = m.p1.y();
            j["brushRadius"] = m.brushRadius;
            j["hardness"] = m.hardness;
            j["autoMask"] = m.autoMask;
            j["paintColor"] = m.paintColor.name(QColor::HexArgb);
            QJsonArray stroke;
            for (const BrushStrokePoint &sp : m.stroke)
                stroke.append(QJsonArray{sp.pt.x(), sp.pt.y(), sp.erase});
            j["stroke"] = stroke;
            QJsonObject ad;
            ad["brightness"] = m.adj.brightness;
            ad["contrast"] = m.adj.contrast;
            ad["highlights"] = m.adj.highlights;
            ad["shadows"] = m.adj.shadows;
            ad["saturation"] = m.adj.saturation;
            ad["vibrance"] = m.adj.vibrance;
            ad["temperature"] = m.adj.temperature;
            ad["tint"] = m.adj.tint;
            ad["wbR"] = m.adj.wbR;
            ad["wbG"] = m.adj.wbG;
            ad["wbB"] = m.adj.wbB;
            ad["denoise"] = m.adj.denoise;
            ad["clarity"] = m.adj.clarity;
            ad["sharpen"] = m.adj.sharpen;
            ad["vignette"] = m.adj.vignette;
            ad["curve"] = curveToJson(m.adj.curve);
            if (!m.adj.levels.isIdentity()) ad["levels"] = levelsToJson(m.adj.levels);
            j["adj"] = ad;
            masks.append(j);
        }
        o["masks"] = masks;
    }

    QJsonArray heals;
    for (const HealOp &hp : a.heals) {
        QJsonObject h;
        h["x"] = hp.x;
        h["y"] = hp.y;
        h["r"] = hp.radius;
        heals.append(h);
    }
    o["heals"] = heals;

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
    a.denoise = o["denoise"].toInt();
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
    a.curve = curveFromJson(o["curve"].toArray());
    if (o.contains("levels")) a.levels = levelsFromJson(o["levels"].toObject());
    for (const QJsonValue &v : o["colorRanges"].toArray()) {
        QJsonObject j = v.toObject();
        ColorRangeAdjust cr;
        cr.r = j["r"].toInt();
        cr.g = j["g"].toInt();
        cr.b = j["b"].toInt();
        cr.channel = j["ch"].toInt();
        cr.amount = j["amt"].toInt();
        a.colorRanges.append(cr);
    }
    for (const QJsonValue &v : o["masks"].toArray()) {
        QJsonObject j = v.toObject();
        Mask m;
        m.name = j["name"].toString();
        m.visible = j["visible"].toBool(true);
        m.opacity = j["opacity"].toDouble(1.0);
        m.blend = static_cast<BlendMode>(j["blend"].toInt(0));
        m.sourceImagePath = j["sourceImagePath"].toString();
        m.sourceImageOffset = QPointF(j["sourceImageOffsetX"].toDouble(0.0),
                                      j["sourceImageOffsetY"].toDouble(0.0));
        m.sourceImageScale = QPointF(j["sourceImageScaleX"].toDouble(1.0),
                                     j["sourceImageScaleY"].toDouble(1.0));
        m.sourceImageLockRatio = j["sourceImageLockRatio"].toBool(true);
        m.type = static_cast<MaskType>(j["type"].toInt(0));
        m.inverted = j["inverted"].toBool();
        m.feather = j["feather"].toDouble(0.5);
        m.center = QPointF(j["cx"].toDouble(0.5), j["cy"].toDouble(0.5));
        m.radiusX = j["rx"].toDouble(0.25);
        m.radiusY = j["ry"].toDouble(0.25);
        m.angle = j["angle"].toDouble(0.0);
        m.p0 = QPointF(j["p0x"].toDouble(0.5), j["p0y"].toDouble(0.2));
        m.p1 = QPointF(j["p1x"].toDouble(0.5), j["p1y"].toDouble(0.6));
        m.brushRadius = j["brushRadius"].toDouble(0.06);
        m.hardness = j["hardness"].toDouble(0.5);
        m.autoMask = j["autoMask"].toBool(false);
        m.paintColor = QColor(j["paintColor"].toString(QStringLiteral("#ff000000")));
        for (const QJsonValue &sv : j["stroke"].toArray()) {
            QJsonArray p = sv.toArray();
            if (p.size() >= 2)
                m.stroke.append(BrushStrokePoint{
                    QPointF(p[0].toDouble(), p[1].toDouble()),
                    p.size() >= 3 && p[2].toBool()});
        }
        QJsonObject ad = j["adj"].toObject();
        m.adj.brightness = ad["brightness"].toInt();
        m.adj.contrast = ad["contrast"].toInt();
        m.adj.highlights = ad["highlights"].toInt();
        m.adj.shadows = ad["shadows"].toInt();
        m.adj.saturation = ad["saturation"].toInt();
        m.adj.vibrance = ad["vibrance"].toInt();
        m.adj.temperature = ad["temperature"].toInt();
        m.adj.tint = ad["tint"].toInt();
        m.adj.wbR = ad["wbR"].toDouble(1.0);
        m.adj.wbG = ad["wbG"].toDouble(1.0);
        m.adj.wbB = ad["wbB"].toDouble(1.0);
        m.adj.denoise = ad["denoise"].toInt();
        m.adj.clarity = ad["clarity"].toInt();
        m.adj.sharpen = ad["sharpen"].toInt();
        m.adj.vignette = ad["vignette"].toInt();
        m.adj.curve = curveFromJson(ad["curve"].toArray());
        if (ad.contains("levels")) m.adj.levels = levelsFromJson(ad["levels"].toObject());
        a.masks.append(m);
    }
    for (const QJsonValue &v : o["heals"].toArray()) {
        QJsonObject h = v.toObject();
        HealOp hp;
        hp.x = h["x"].toInt();
        hp.y = h["y"].toInt();
        hp.radius = h["r"].toInt();
        a.heals.append(hp);
    }
    out = a;
    return true;
}

QString thumbnailPathFor(const QString &imagePath) {
    return imagePath + ".nte.thumb.jpg";
}

bool saveThumbnail(const QString &imagePath, const QImage &image) {
    if (image.isNull()) return false;
    // Cap the cached thumbnail's size — the filmstrip icon is tiny, so a modest
    // JPEG keeps the sidecar footprint small.
    QImage scaled = image.width() > 320
                        ? image.scaledToWidth(320, Qt::SmoothTransformation)
                        : image;
    return ditherTo8Bit(scaled).save(thumbnailPathFor(imagePath), "JPEG", 85);
}

QImage loadThumbnail(const QString &imagePath) {
    QImage img;
    img.load(thumbnailPathFor(imagePath), "JPEG");
    return img;
}

} // namespace EditSidecar
