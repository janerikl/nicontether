#include "edit/EditSidecar.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QByteArray>

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
    // Rating lives in the same sidecar file but isn't part of Adjustments, so
    // carry over whatever was there before this rewrite.
    int existingRating = loadRating(imagePath);

    QJsonObject o;
    o["version"] = 6;
    if (existingRating > 0) o["rating"] = existingRating;
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
    o["lightAngle"] = a.lightAngle;
    o["lightIntensity"] = a.lightIntensity;
    o["flatStyle"] = a.flatStyle;
    o["rotationQuadrants"] = a.rotationQuadrants;
    o["flipH"] = a.flipH;
    o["flipV"] = a.flipV;
    o["backgroundColor"] = a.backgroundColor.name(QColor::HexRgb);
    o["backgroundHidden"] = a.backgroundHidden;
    o["backgroundDeleted"] = a.backgroundDeleted;
    if (!a.cropRect.isNull()) {
        QJsonObject c;
        c["x"] = a.cropRect.x();
        c["y"] = a.cropRect.y();
        c["w"] = a.cropRect.width();
        c["h"] = a.cropRect.height();
        c["angle"] = a.cropAngle;
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
            j["text"] = m.text;
            j["textFamily"] = m.textFamily;
            j["textPixelSize"] = m.textPixelSize;
            j["textBold"] = m.textBold;
            j["textItalic"] = m.textItalic;
            j["textPosX"] = m.textPos.x();
            j["textPosY"] = m.textPos.y();
            QJsonArray stroke;
            for (const BrushStrokePoint &sp : m.stroke)
                stroke.append(QJsonArray{sp.pt.x(), sp.pt.y(), sp.erase, sp.radius, sp.hardness,
                                          double(sp.color)});
            j["stroke"] = stroke;
            QJsonArray erases;
            for (const ErasePoint &ep : m.eraseStrokes)
                erases.append(QJsonArray{ep.pt.x(), ep.pt.y(), ep.radius});
            j["eraseStrokes"] = erases;
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
            ad["lightAngle"] = m.adj.lightAngle;
            ad["lightIntensity"] = m.adj.lightIntensity;
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

    if (!a.texts.isEmpty()) {
        QJsonArray texts;
        for (const TextOp &t : a.texts) {
            QJsonObject j;
            j["x"] = t.pos.x();
            j["y"] = t.pos.y();
            j["rotation"] = t.rotation;
            j["text"] = t.text;
            j["family"] = t.family;
            j["pixelSize"] = t.pixelSize;
            j["bold"] = t.bold;
            j["italic"] = t.italic;
            j["color"] = t.color.name(QColor::HexArgb);
            j["outlineEnabled"] = t.outlineEnabled;
            j["outlineColor"] = t.outlineColor.name(QColor::HexArgb);
            j["outlineWidth"] = t.outlineWidth;
            j["shadowEnabled"] = t.shadowEnabled;
            j["shadowOffsetX"] = t.shadowOffset.x();
            j["shadowOffsetY"] = t.shadowOffset.y();
            j["shadowBlur"] = t.shadowBlur;
            j["shadowOpacity"] = t.shadowOpacity;
            j["shadowColor"] = t.shadowColor.name(QColor::HexArgb);
            j["bgEnabled"] = t.bgEnabled;
            j["bgColor"] = t.bgColor.name(QColor::HexArgb);
            j["bgOpacity"] = t.bgOpacity;
            j["bgPadding"] = t.bgPadding;
            texts.append(j);
        }
        o["texts"] = texts;
    }

    if (!a.shapes.isEmpty()) {
        QJsonArray shapes;
        auto shapeTypeName = [](ShapeType t) {
            switch (t) {
            case ShapeType::Rectangle: return "rectangle";
            case ShapeType::Ellipse:   return "ellipse";
            case ShapeType::Line:      return "line";
            case ShapeType::Polygon:   return "polygon";
            case ShapeType::Star:      return "star";
            case ShapeType::Heart:     return "heart";
            }
            return "rectangle";
        };
        for (const ShapeOp &s : a.shapes) {
            QJsonObject j;
            j["type"] = shapeTypeName(s.type);
            j["rectX"] = s.rect.x();
            j["rectY"] = s.rect.y();
            j["rectW"] = s.rect.width();
            j["rectH"] = s.rect.height();
            j["p1x"] = s.p1.x();
            j["p1y"] = s.p1.y();
            j["p2x"] = s.p2.x();
            j["p2y"] = s.p2.y();
            j["rotation"] = s.rotation;
            j["sides"] = s.sides;
            j["innerRadiusRatio"] = s.innerRadiusRatio;
            j["fillEnabled"] = s.fillEnabled;
            j["fillColor"] = s.fillColor.name(QColor::HexArgb);
            j["strokeEnabled"] = s.strokeEnabled;
            j["strokeColor"] = s.strokeColor.name(QColor::HexArgb);
            j["strokeWidth"] = s.strokeWidth;
            j["opacity"] = s.opacity;
            j["visible"] = s.visible;
            j["groupId"] = s.groupId;
            shapes.append(j);
        }
        o["shapes"] = shapes;
    }

    if (!a.removals.isEmpty()) {
        // Cached fill/mask images are embedded as base64-encoded PNG so a
        // reloaded sidecar restores the exact same content-aware result
        // without recomputing InpaintTool::inpaint from scratch (unlike
        // heals, which cheaply re-derive their pixels on every load).
        auto imageToBase64Png = [](const QImage &img) -> QString {
            if (img.isNull()) return QString();
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            return QString::fromLatin1(bytes.toBase64());
        };
        QJsonArray removals;
        for (const RemoveObjectOp &r : a.removals) {
            QJsonObject j;
            QJsonArray stroke;
            for (const QPointF &p : r.stroke) stroke.append(QJsonArray{p.x(), p.y()});
            j["stroke"] = stroke;
            j["radius"] = r.radius;
            j["rectX"] = r.rect.x();
            j["rectY"] = r.rect.y();
            j["rectW"] = r.rect.width();
            j["rectH"] = r.rect.height();
            j["mask"] = imageToBase64Png(r.mask);
            j["fill"] = imageToBase64Png(r.fill);
            j["visible"] = r.visible;
            removals.append(j);
        }
        o["removals"] = removals;
    }

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
    a.lightAngle = o["lightAngle"].toInt();
    a.lightIntensity = o["lightIntensity"].toInt();
    a.flatStyle = o["flatStyle"].toInt();
    a.rotationQuadrants = o["rotationQuadrants"].toInt();
    a.flipH = o["flipH"].toBool();
    a.flipV = o["flipV"].toBool();
    a.backgroundColor = QColor(o["backgroundColor"].toString(QStringLiteral("#1e1e1e")));
    if (!a.backgroundColor.isValid()) a.backgroundColor = QColor(30, 30, 30);
    a.backgroundHidden = o["backgroundHidden"].toBool();
    a.backgroundDeleted = o["backgroundDeleted"].toBool();
    if (o.contains("crop")) {
        QJsonObject c = o["crop"].toObject();
        a.cropRect = QRect(c["x"].toInt(), c["y"].toInt(),
                           c["w"].toInt(), c["h"].toInt());
        a.cropAngle = c["angle"].toDouble();
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
        m.text = j["text"].toString();
        m.textFamily = j["textFamily"].toString(QStringLiteral("Sans Serif"));
        m.textPixelSize = j["textPixelSize"].toDouble(0.08);
        m.textBold = j["textBold"].toBool(false);
        m.textItalic = j["textItalic"].toBool(false);
        m.textPos = QPointF(j["textPosX"].toDouble(0.3), j["textPosY"].toDouble(0.45));
        for (const QJsonValue &sv : j["stroke"].toArray()) {
            QJsonArray p = sv.toArray();
            if (p.size() >= 2)
                // radius/hardness/color fall back to the mask's own values
                // for sidecars saved before per-point brush settings were tracked.
                m.stroke.append(BrushStrokePoint{
                    QPointF(p[0].toDouble(), p[1].toDouble()),
                    p.size() >= 3 && p[2].toBool(),
                    p.size() >= 4 ? p[3].toDouble() : m.brushRadius,
                    p.size() >= 5 ? p[4].toDouble() : m.hardness,
                    p.size() >= 6 ? QRgb(qint64(p[5].toDouble())) : m.paintColor.rgb()});
        }
        for (const QJsonValue &ev : j["eraseStrokes"].toArray()) {
            QJsonArray p = ev.toArray();
            if (p.size() >= 3)
                m.eraseStrokes.append(ErasePoint{
                    QPointF(p[0].toDouble(), p[1].toDouble()), p[2].toDouble()});
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
        m.adj.lightAngle = ad["lightAngle"].toInt();
        m.adj.lightIntensity = ad["lightIntensity"].toInt();
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
    for (const QJsonValue &v : o["texts"].toArray()) {
        QJsonObject j = v.toObject();
        TextOp t;
        t.pos = QPointF(j["x"].toDouble(0.0), j["y"].toDouble(0.0));
        t.rotation = j["rotation"].toDouble(0.0);
        t.text = j["text"].toString();
        t.family = j["family"].toString(QStringLiteral("Sans Serif"));
        t.pixelSize = j["pixelSize"].toDouble(48.0);
        t.bold = j["bold"].toBool(false);
        t.italic = j["italic"].toBool(false);
        t.color = QColor(j["color"].toString(QStringLiteral("#ffffffff")));
        t.outlineEnabled = j["outlineEnabled"].toBool(false);
        t.outlineColor = QColor(j["outlineColor"].toString(QStringLiteral("#ff000000")));
        t.outlineWidth = j["outlineWidth"].toDouble(3.0);
        t.shadowEnabled = j["shadowEnabled"].toBool(false);
        t.shadowOffset = QPointF(j["shadowOffsetX"].toDouble(4.0), j["shadowOffsetY"].toDouble(4.0));
        t.shadowBlur = j["shadowBlur"].toDouble(6.0);
        t.shadowOpacity = j["shadowOpacity"].toDouble(0.6);
        t.shadowColor = QColor(j["shadowColor"].toString(QStringLiteral("#ff000000")));
        t.bgEnabled = j["bgEnabled"].toBool(false);
        t.bgColor = QColor(j["bgColor"].toString(QStringLiteral("#ff000000")));
        t.bgOpacity = j["bgOpacity"].toDouble(0.6);
        t.bgPadding = j["bgPadding"].toDouble(10.0);
        a.texts.append(t);
    }
    auto shapeTypeFromName = [](const QString &name) {
        if (name == "ellipse") return ShapeType::Ellipse;
        if (name == "line") return ShapeType::Line;
        if (name == "polygon") return ShapeType::Polygon;
        if (name == "star") return ShapeType::Star;
        if (name == "heart") return ShapeType::Heart;
        return ShapeType::Rectangle;
    };
    for (const QJsonValue &v : o["shapes"].toArray()) {
        QJsonObject j = v.toObject();
        ShapeOp s;
        s.type = shapeTypeFromName(j["type"].toString());
        s.rect = QRectF(j["rectX"].toDouble(0.0), j["rectY"].toDouble(0.0),
                        j["rectW"].toDouble(200.0), j["rectH"].toDouble(200.0));
        s.p1 = QPointF(j["p1x"].toDouble(0.0), j["p1y"].toDouble(0.0));
        s.p2 = QPointF(j["p2x"].toDouble(200.0), j["p2y"].toDouble(0.0));
        s.rotation = j["rotation"].toDouble(0.0);
        s.sides = j["sides"].toInt(5);
        s.innerRadiusRatio = j["innerRadiusRatio"].toDouble(0.5);
        s.fillEnabled = j["fillEnabled"].toBool(true);
        s.fillColor = QColor(j["fillColor"].toString(QStringLiteral("#ffffffff")));
        s.strokeEnabled = j["strokeEnabled"].toBool(true);
        s.strokeColor = QColor(j["strokeColor"].toString(QStringLiteral("#ff000000")));
        s.strokeWidth = j["strokeWidth"].toDouble(4.0);
        s.opacity = j["opacity"].toDouble(1.0);
        s.visible = j["visible"].toBool(true);
        s.groupId = j["groupId"].toString();
        a.shapes.append(s);
    }
    auto imageFromBase64Png = [](const QString &b64) -> QImage {
        if (b64.isEmpty()) return QImage();
        QByteArray bytes = QByteArray::fromBase64(b64.toLatin1());
        QImage img;
        img.loadFromData(bytes, "PNG");
        return img;
    };
    for (const QJsonValue &v : o["removals"].toArray()) {
        QJsonObject j = v.toObject();
        RemoveObjectOp r;
        for (const QJsonValue &pv : j["stroke"].toArray()) {
            QJsonArray p = pv.toArray();
            r.stroke.append(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
        }
        r.radius = j["radius"].toDouble(20.0);
        r.rect = QRect(j["rectX"].toInt(0), j["rectY"].toInt(0),
                       j["rectW"].toInt(0), j["rectH"].toInt(0));
        r.mask = imageFromBase64Png(j["mask"].toString());
        r.fill = imageFromBase64Png(j["fill"].toString());
        r.visible = j["visible"].toBool(true);
        if (!r.rect.isEmpty() && !r.fill.isNull()) a.removals.append(r);
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

int loadRating(const QString &imagePath) {
    QFile f(pathFor(imagePath));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return 0;
    return doc.object()["rating"].toInt(0);
}

bool saveRating(const QString &imagePath, int rating) {
    QFile f(pathFor(imagePath));
    QJsonObject o;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) o = doc.object();
        f.close();
    }
    if (rating > 0)
        o["rating"] = rating;
    else
        o.remove("rating");
    if (!o.contains("version")) o["version"] = 6;

    QFile out(pathFor(imagePath));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace EditSidecar
