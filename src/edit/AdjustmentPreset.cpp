#include "edit/AdjustmentPreset.h"

#include <QSettings>
#include <QStringList>

namespace {

QString curveToString(const QVector<QPointF> &curve) {
    QStringList parts;
    for (const QPointF &p : curve)
        parts << QString("%1,%2").arg(p.x()).arg(p.y());
    return parts.join(';');
}

QVector<QPointF> curveFromString(const QString &s) {
    QVector<QPointF> curve;
    for (const QString &pair : s.split(';', Qt::SkipEmptyParts)) {
        QStringList xy = pair.split(',');
        if (xy.size() == 2) curve.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
    }
    return curve;
}

} // namespace

QList<AdjustmentPreset> AdjustmentPresetStore::builtins() {
    QList<AdjustmentPreset> b;
    auto add = [&](const QString &name, Adjustments a) {
        AdjustmentPreset p;
        p.name = name;
        p.adj = a;
        p.builtIn = true;
        b.append(p);
    };

    add("None (Reset)", Adjustments());

    Adjustments bw;
    bw.saturation = -100;
    add("Black & White", bw);

    Adjustments warm;
    warm.temperature = 30;
    add("Warm", warm);

    Adjustments cool;
    cool.temperature = -30;
    add("Cool", cool);

    Adjustments highContrast;
    highContrast.contrast = 40;
    highContrast.highlights = -10;
    highContrast.shadows = -10;
    add("High Contrast", highContrast);

    Adjustments vivid;
    vivid.vibrance = 40;
    vivid.saturation = 15;
    vivid.clarity = 10;
    add("Vivid", vivid);

    Adjustments soft;
    soft.contrast = -15;
    soft.clarity = -20;
    soft.shadows = 15;
    soft.highlights = -10;
    add("Soft / Matte", soft);

    // Approximates a bold cartoon/painting look using only tone/colour
    // sliders (no stylization filter exists yet): punchy saturation, flattened
    // shadows and boosted clarity/contrast for a graphic, poster-like edge.
    Adjustments cartoon;
    cartoon.saturation = 60;
    cartoon.vibrance = 30;
    cartoon.clarity = 60;
    cartoon.contrast = 35;
    cartoon.shadows = -20;
    cartoon.sharpen = 20;
    add("Cartoon / Painting", cartoon);

    return b;
}

QList<AdjustmentPreset> AdjustmentPresetStore::all() const {
    return builtins() + m_custom;
}

bool AdjustmentPresetStore::isCustom(const QString &name) const {
    for (const AdjustmentPreset &p : m_custom)
        if (p.name == name) return true;
    return false;
}

void AdjustmentPresetStore::addOrUpdate(const AdjustmentPreset &p) {
    AdjustmentPreset entry = p;
    entry.builtIn = false;
    for (AdjustmentPreset &e : m_custom) {
        if (e.name == entry.name) {
            e = entry;
            save();
            return;
        }
    }
    m_custom.append(entry);
    save();
}

void AdjustmentPresetStore::remove(const QString &name) {
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].name == name) {
            m_custom.removeAt(i);
            save();
            return;
        }
    }
}

void AdjustmentPresetStore::load() {
    m_custom.clear();
    QSettings s;
    int n = s.beginReadArray("adjustmentPresets");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        AdjustmentPreset p;
        p.name = s.value("name").toString();
        p.builtIn = false;
        Adjustments &a = p.adj;
        a.brightness = s.value("brightness").toInt();
        a.contrast = s.value("contrast").toInt();
        a.highlights = s.value("highlights").toInt();
        a.shadows = s.value("shadows").toInt();
        a.saturation = s.value("saturation").toInt();
        a.vibrance = s.value("vibrance").toInt();
        a.temperature = s.value("temperature").toInt();
        a.tint = s.value("tint").toInt();
        a.wbR = s.value("wbR", 1.0).toDouble();
        a.wbG = s.value("wbG", 1.0).toDouble();
        a.wbB = s.value("wbB", 1.0).toDouble();
        a.denoise = s.value("denoise").toInt();
        a.clarity = s.value("clarity").toInt();
        a.sharpen = s.value("sharpen").toInt();
        a.vignette = s.value("vignette").toInt();
        a.lightAngle = s.value("lightAngle").toInt();
        a.lightIntensity = s.value("lightIntensity").toInt();
        a.flatStyle = s.value("flatStyle").toInt();
        a.curve = curveFromString(s.value("curve").toString());
        a.levels.rgb.inBlack = s.value("levels.rgb.inBlack", 0).toInt();
        a.levels.rgb.inWhite = s.value("levels.rgb.inWhite", 255).toInt();
        a.levels.rgb.gamma = s.value("levels.rgb.gamma", 1.0).toDouble();
        a.levels.rgb.outBlack = s.value("levels.rgb.outBlack", 0).toInt();
        a.levels.rgb.outWhite = s.value("levels.rgb.outWhite", 255).toInt();
        a.levels.r.inBlack = s.value("levels.r.inBlack", 0).toInt();
        a.levels.r.inWhite = s.value("levels.r.inWhite", 255).toInt();
        a.levels.r.gamma = s.value("levels.r.gamma", 1.0).toDouble();
        a.levels.r.outBlack = s.value("levels.r.outBlack", 0).toInt();
        a.levels.r.outWhite = s.value("levels.r.outWhite", 255).toInt();
        a.levels.g.inBlack = s.value("levels.g.inBlack", 0).toInt();
        a.levels.g.inWhite = s.value("levels.g.inWhite", 255).toInt();
        a.levels.g.gamma = s.value("levels.g.gamma", 1.0).toDouble();
        a.levels.g.outBlack = s.value("levels.g.outBlack", 0).toInt();
        a.levels.g.outWhite = s.value("levels.g.outWhite", 255).toInt();
        a.levels.b.inBlack = s.value("levels.b.inBlack", 0).toInt();
        a.levels.b.inWhite = s.value("levels.b.inWhite", 255).toInt();
        a.levels.b.gamma = s.value("levels.b.gamma", 1.0).toDouble();
        a.levels.b.outBlack = s.value("levels.b.outBlack", 0).toInt();
        a.levels.b.outWhite = s.value("levels.b.outWhite", 255).toInt();
        if (!p.name.isEmpty()) m_custom.append(p);
    }
    s.endArray();
}

void AdjustmentPresetStore::save() const {
    QSettings s;
    s.beginWriteArray("adjustmentPresets");
    for (int i = 0; i < m_custom.size(); ++i) {
        s.setArrayIndex(i);
        const AdjustmentPreset &p = m_custom[i];
        const Adjustments &a = p.adj;
        s.setValue("name", p.name);
        s.setValue("brightness", a.brightness);
        s.setValue("contrast", a.contrast);
        s.setValue("highlights", a.highlights);
        s.setValue("shadows", a.shadows);
        s.setValue("saturation", a.saturation);
        s.setValue("vibrance", a.vibrance);
        s.setValue("temperature", a.temperature);
        s.setValue("tint", a.tint);
        s.setValue("wbR", a.wbR);
        s.setValue("wbG", a.wbG);
        s.setValue("wbB", a.wbB);
        s.setValue("denoise", a.denoise);
        s.setValue("clarity", a.clarity);
        s.setValue("sharpen", a.sharpen);
        s.setValue("vignette", a.vignette);
        s.setValue("lightAngle", a.lightAngle);
        s.setValue("lightIntensity", a.lightIntensity);
        s.setValue("flatStyle", a.flatStyle);
        s.setValue("curve", curveToString(a.curve));
        s.setValue("levels.rgb.inBlack", a.levels.rgb.inBlack);
        s.setValue("levels.rgb.inWhite", a.levels.rgb.inWhite);
        s.setValue("levels.rgb.gamma", a.levels.rgb.gamma);
        s.setValue("levels.rgb.outBlack", a.levels.rgb.outBlack);
        s.setValue("levels.rgb.outWhite", a.levels.rgb.outWhite);
        s.setValue("levels.r.inBlack", a.levels.r.inBlack);
        s.setValue("levels.r.inWhite", a.levels.r.inWhite);
        s.setValue("levels.r.gamma", a.levels.r.gamma);
        s.setValue("levels.r.outBlack", a.levels.r.outBlack);
        s.setValue("levels.r.outWhite", a.levels.r.outWhite);
        s.setValue("levels.g.inBlack", a.levels.g.inBlack);
        s.setValue("levels.g.inWhite", a.levels.g.inWhite);
        s.setValue("levels.g.gamma", a.levels.g.gamma);
        s.setValue("levels.g.outBlack", a.levels.g.outBlack);
        s.setValue("levels.g.outWhite", a.levels.g.outWhite);
        s.setValue("levels.b.inBlack", a.levels.b.inBlack);
        s.setValue("levels.b.inWhite", a.levels.b.inWhite);
        s.setValue("levels.b.gamma", a.levels.b.gamma);
        s.setValue("levels.b.outBlack", a.levels.b.outBlack);
        s.setValue("levels.b.outWhite", a.levels.b.outWhite);
    }
    s.endArray();
}
