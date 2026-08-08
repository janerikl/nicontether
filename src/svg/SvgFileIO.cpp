#include "svg/SvgFileIO.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "svg/SvgRender.h"

namespace {

QString pathToSvgD(const QPainterPath &path) {
    QString d;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element &e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            d += QString("M%1,%2 ").arg(e.x).arg(e.y);
            break;
        case QPainterPath::LineToElement:
            d += QString("L%1,%2 ").arg(e.x).arg(e.y);
            break;
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element &c1 = path.elementAt(i + 1);
            const QPainterPath::Element &c2 = path.elementAt(i + 2);
            d += QString("C%1,%2 %3,%4 %5,%6 ")
                     .arg(e.x).arg(e.y).arg(c1.x).arg(c1.y).arg(c2.x).arg(c2.y);
            i += 2;
            break;
        }
        default:
            break;
        }
    }
    return d.trimmed();
}

QPainterPath svgDToPath(const QString &d) {
    QPainterPath path;
    static const QRegularExpression tokenRe(
        R"(([MLCZ])|(-?\d*\.?\d+(?:[eE][-+]?\d+)?))");
    auto it = tokenRe.globalMatch(d);
    QChar cmd;
    QVector<qreal> nums;

    auto flush = [&]() {
        if (cmd == 'M' && nums.size() >= 2) path.moveTo(nums[0], nums[1]);
        else if (cmd == 'L' && nums.size() >= 2) path.lineTo(nums[0], nums[1]);
        else if (cmd == 'C' && nums.size() >= 6) path.cubicTo(nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]);
        else if (cmd == 'Z') path.closeSubpath();
        nums.clear();
    };

    while (it.hasNext()) {
        auto match = it.next();
        if (!match.captured(1).isEmpty()) {
            if (!cmd.isNull()) flush();
            cmd = match.captured(1)[0];
            if (cmd == 'Z') flush();
        } else {
            nums << match.captured(2).toDouble();
        }
    }
    if (!cmd.isNull() && cmd != 'Z') flush();
    return path;
}

QString colorHex(const QColor &c) { return c.name(QColor::HexRgb); }

void writeFillStroke(QXmlStreamWriter &xml, const SvgNode &node, QVector<SvgNode> &gradientOwners) {
    if (node.fillType == SvgFillType::None) {
        xml.writeAttribute("fill", "none");
    } else if (node.fillType == SvgFillType::Solid) {
        xml.writeAttribute("fill", colorHex(node.fillColor));
        xml.writeAttribute("fill-opacity", QString::number(node.fillColor.alphaF()));
    } else {
        int gradId = gradientOwners.size();
        gradientOwners << node;
        xml.writeAttribute("fill", QString("url(#grad%1)").arg(gradId));
    }

    if (node.strokeEnabled) {
        xml.writeAttribute("stroke", colorHex(node.strokeColor));
        xml.writeAttribute("stroke-opacity", QString::number(node.strokeColor.alphaF()));
        xml.writeAttribute("stroke-width", QString::number(node.strokeWidth));
        if (!node.strokeDashPattern.isEmpty()) {
            QStringList parts;
            for (qreal v : node.strokeDashPattern) parts << QString::number(v);
            xml.writeAttribute("stroke-dasharray", parts.join(","));
        }
    } else {
        xml.writeAttribute("stroke", "none");
    }
}

QString transformAttr(const SvgNode &node) {
    return QString("translate(%1,%2) rotate(%3) scale(%4,%5)")
        .arg(node.position.x()).arg(node.position.y())
        .arg(node.rotationDegrees).arg(node.scaleX).arg(node.scaleY);
}

} // namespace

namespace SvgFileIO {

bool save(const SvgDocument &doc, const QString &path, QString *errorOut) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = file.errorString();
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("svg");
    xml.writeAttribute("xmlns", "http://www.w3.org/2000/svg");
    xml.writeAttribute("width", QString::number(doc.canvasSize.width()));
    xml.writeAttribute("height", QString::number(doc.canvasSize.height()));
    xml.writeAttribute("viewBox", QString("0 0 %1 %2").arg(doc.canvasSize.width()).arg(doc.canvasSize.height()));
    if (doc.backgroundColor.alpha() > 0) {
        xml.writeAttribute("data-bg", colorHex(doc.backgroundColor));
        xml.writeAttribute("data-bg-opacity", QString::number(doc.backgroundColor.alphaF()));
    }

    QVector<SvgNode> gradientOwners;

    // Write nodes first into a buffered string so we know all gradients
    // referenced before emitting <defs> (kept minimal: no editor supports
    // shared/re-used gradient defs yet, one <linearGradient>/<radialGradient>
    // per gradient-filled node).
    QString nodesXml;
    {
        QXmlStreamWriter nx(&nodesXml);
        nx.setAutoFormatting(true);
        for (const SvgNode &node : doc.nodes) {
            switch (node.type) {
            case SvgNodeType::Group:
                nx.writeStartElement("g");
                nx.writeAttribute("id", node.id);
                nx.writeAttribute("data-name", node.name);
                nx.writeAttribute("data-children", node.childIds.join(","));
                nx.writeEndElement();
                continue;
            case SvgNodeType::Rect:
                nx.writeStartElement("rect");
                nx.writeAttribute("x", QString::number(node.rect.x()));
                nx.writeAttribute("y", QString::number(node.rect.y()));
                nx.writeAttribute("width", QString::number(node.rect.width()));
                nx.writeAttribute("height", QString::number(node.rect.height()));
                if (node.rectCornerRadius > 0.0)
                    nx.writeAttribute("rx", QString::number(node.rectCornerRadius));
                break;
            case SvgNodeType::Ellipse:
                nx.writeStartElement("ellipse");
                nx.writeAttribute("cx", QString::number(node.rect.center().x()));
                nx.writeAttribute("cy", QString::number(node.rect.center().y()));
                nx.writeAttribute("rx", QString::number(node.rect.width() / 2.0));
                nx.writeAttribute("ry", QString::number(node.rect.height() / 2.0));
                break;
            case SvgNodeType::Line:
                nx.writeStartElement("line");
                nx.writeAttribute("x1", QString::number(node.lineP1.x()));
                nx.writeAttribute("y1", QString::number(node.lineP1.y()));
                nx.writeAttribute("x2", QString::number(node.lineP2.x()));
                nx.writeAttribute("y2", QString::number(node.lineP2.y()));
                break;
            case SvgNodeType::Polygon:
            case SvgNodeType::Star:
                nx.writeStartElement("path");
                nx.writeAttribute("data-shape", node.type == SvgNodeType::Star ? "star" : "polygon");
                nx.writeAttribute("data-sides", QString::number(node.polygonSides));
                nx.writeAttribute("data-radius", QString::number(node.polygonRadius));
                nx.writeAttribute("data-inner-ratio", QString::number(node.starInnerRadiusRatio));
                nx.writeAttribute("d", pathToSvgD(svgNodeLocalPath(node)));
                break;
            case SvgNodeType::Path:
                nx.writeStartElement("path");
                nx.writeAttribute("d", pathToSvgD(node.path));
                break;
            case SvgNodeType::Text:
                nx.writeStartElement("text");
                nx.writeAttribute("x", QString::number(node.textOrigin.x()));
                nx.writeAttribute("y", QString::number(node.textOrigin.y()));
                nx.writeAttribute("font-family", node.font.family());
                nx.writeAttribute("font-size", QString::number(node.font.pointSizeF() > 0 ? node.font.pointSizeF() : 16.0));
                break;
            }

            nx.writeAttribute("id", node.id);
            nx.writeAttribute("data-name", node.name);
            nx.writeAttribute("data-visible", node.visible ? "1" : "0");
            nx.writeAttribute("data-locked", node.locked ? "1" : "0");
            if (!node.parentGroupId.isEmpty()) nx.writeAttribute("data-parent", node.parentGroupId);
            nx.writeAttribute("opacity", QString::number(node.opacity));
            nx.writeAttribute("transform", transformAttr(node));
            writeFillStroke(nx, node, gradientOwners);

            if (node.type == SvgNodeType::Text) nx.writeCharacters(node.text);
            nx.writeEndElement();
        }
    }

    if (!gradientOwners.isEmpty()) {
        xml.writeStartElement("defs");
        for (int i = 0; i < gradientOwners.size(); ++i) {
            const SvgGradient &g = gradientOwners[i].fillGradient;
            if (g.type == SvgGradientType::Linear) {
                xml.writeStartElement("linearGradient");
                xml.writeAttribute("id", QString("grad%1").arg(i));
                xml.writeAttribute("x1", QString::number(g.linearStart.x()));
                xml.writeAttribute("y1", QString::number(g.linearStart.y()));
                xml.writeAttribute("x2", QString::number(g.linearEnd.x()));
                xml.writeAttribute("y2", QString::number(g.linearEnd.y()));
            } else {
                xml.writeStartElement("radialGradient");
                xml.writeAttribute("id", QString("grad%1").arg(i));
                xml.writeAttribute("cx", QString::number(g.radialCenter.x()));
                xml.writeAttribute("cy", QString::number(g.radialCenter.y()));
                xml.writeAttribute("fx", QString::number(g.radialFocal.x()));
                xml.writeAttribute("fy", QString::number(g.radialFocal.y()));
                xml.writeAttribute("r", QString::number(g.radialRadius));
            }
            for (const auto &stop : g.stops) {
                xml.writeStartElement("stop");
                xml.writeAttribute("offset", QString::number(stop.position));
                xml.writeAttribute("stop-color", colorHex(stop.color));
                xml.writeAttribute("stop-opacity", QString::number(stop.color.alphaF()));
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement(); // defs
    }

    xml.device()->write(nodesXml.toUtf8());
    xml.writeEndElement(); // svg
    xml.writeEndDocument();
    return true;
}

bool load(SvgDocument &doc, const QString &path, QString *errorOut) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = file.errorString();
        return false;
    }

    SvgDocument result;
    QMap<QString, SvgGradient> gradients;

    QXmlStreamReader xml(&file);
    QString currentGradientId;
    SvgGradient *currentGradient = nullptr;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const QString tag = xml.name().toString();
        const auto &attrs = xml.attributes();

        if (tag == "svg") {
            result.canvasSize = QSizeF(attrs.value("width").toDouble(), attrs.value("height").toDouble());
            if (attrs.hasAttribute("data-bg")) {
                QColor c(attrs.value("data-bg").toString());
                c.setAlphaF(attrs.value("data-bg-opacity").toDouble());
                result.backgroundColor = c;
            }
        } else if (tag == "linearGradient" || tag == "radialGradient") {
            currentGradientId = attrs.value("id").toString();
            SvgGradient g;
            g.type = tag == "linearGradient" ? SvgGradientType::Linear : SvgGradientType::Radial;
            if (tag == "linearGradient") {
                g.linearStart = QPointF(attrs.value("x1").toDouble(), attrs.value("y1").toDouble());
                g.linearEnd = QPointF(attrs.value("x2").toDouble(), attrs.value("y2").toDouble());
            } else {
                g.radialCenter = QPointF(attrs.value("cx").toDouble(), attrs.value("cy").toDouble());
                g.radialFocal = QPointF(attrs.value("fx").toDouble(), attrs.value("fy").toDouble());
                g.radialRadius = attrs.value("r").toDouble();
            }
            gradients[currentGradientId] = g;
            currentGradient = &gradients[currentGradientId];
        } else if (tag == "stop" && currentGradient) {
            SvgGradientStop stop;
            stop.position = attrs.value("offset").toDouble();
            QColor c(attrs.value("stop-color").toString());
            c.setAlphaF(attrs.hasAttribute("stop-opacity") ? attrs.value("stop-opacity").toDouble() : 1.0);
            stop.color = c;
            currentGradient->stops << stop;
        } else if (tag == "g") {
            SvgNode node;
            node.type = SvgNodeType::Group;
            node.id = attrs.value("id").toString();
            node.name = attrs.value("data-name").toString();
            QString children = attrs.value("data-children").toString();
            if (!children.isEmpty()) node.childIds = children.split(",");
            result.nodes << node;
        } else if (tag == "rect" || tag == "ellipse" || tag == "line" || tag == "path" || tag == "text") {
            SvgNode node;
            if (tag == "rect") {
                node.type = SvgNodeType::Rect;
                node.rect = QRectF(attrs.value("x").toDouble(), attrs.value("y").toDouble(),
                                    attrs.value("width").toDouble(), attrs.value("height").toDouble());
                node.rectCornerRadius = attrs.value("rx").toDouble();
            } else if (tag == "ellipse") {
                node.type = SvgNodeType::Ellipse;
                qreal cx = attrs.value("cx").toDouble(), cy = attrs.value("cy").toDouble();
                qreal rx = attrs.value("rx").toDouble(), ry = attrs.value("ry").toDouble();
                node.rect = QRectF(cx - rx, cy - ry, rx * 2, ry * 2);
            } else if (tag == "line") {
                node.type = SvgNodeType::Line;
                node.lineP1 = QPointF(attrs.value("x1").toDouble(), attrs.value("y1").toDouble());
                node.lineP2 = QPointF(attrs.value("x2").toDouble(), attrs.value("y2").toDouble());
            } else if (tag == "path") {
                QString shape = attrs.value("data-shape").toString();
                if (shape == "polygon" || shape == "star") {
                    node.type = shape == "star" ? SvgNodeType::Star : SvgNodeType::Polygon;
                    node.polygonSides = attrs.value("data-sides").toInt();
                    node.polygonRadius = attrs.value("data-radius").toDouble();
                    node.starInnerRadiusRatio = attrs.value("data-inner-ratio").toDouble();
                } else {
                    node.type = SvgNodeType::Path;
                    node.path = svgDToPath(attrs.value("d").toString());
                }
            } else if (tag == "text") {
                node.type = SvgNodeType::Text;
                node.textOrigin = QPointF(attrs.value("x").toDouble(), attrs.value("y").toDouble());
                node.font.setFamily(attrs.value("font-family").toString());
                node.font.setPointSizeF(attrs.value("font-size").toDouble());
                node.text = xml.readElementText();
            }

            node.id = attrs.value("id").toString();
            node.name = attrs.value("data-name").toString();
            node.visible = attrs.value("data-visible").toString() != "0";
            node.locked = attrs.value("data-locked").toString() == "1";
            node.parentGroupId = attrs.value("data-parent").toString();
            node.opacity = attrs.hasAttribute("opacity") ? attrs.value("opacity").toDouble() : 1.0;

            QString transform = attrs.value("transform").toString();
            static const QRegularExpression translateRe(R"(translate\(([-\d.eE+]+),([-\d.eE+]+)\))");
            static const QRegularExpression rotateRe(R"(rotate\(([-\d.eE+]+)\))");
            static const QRegularExpression scaleRe(R"(scale\(([-\d.eE+]+),([-\d.eE+]+)\))");
            if (auto m = translateRe.match(transform); m.hasMatch())
                node.position = QPointF(m.captured(1).toDouble(), m.captured(2).toDouble());
            if (auto m = rotateRe.match(transform); m.hasMatch())
                node.rotationDegrees = m.captured(1).toDouble();
            if (auto m = scaleRe.match(transform); m.hasMatch()) {
                node.scaleX = m.captured(1).toDouble();
                node.scaleY = m.captured(2).toDouble();
            }

            QString fill = attrs.value("fill").toString();
            if (fill == "none") {
                node.fillType = SvgFillType::None;
            } else if (fill.startsWith("url(#")) {
                node.fillType = SvgFillType::Gradient;
                QString gradId = fill.mid(5, fill.size() - 6);
                if (gradients.contains(gradId)) node.fillGradient = gradients[gradId];
            } else if (!fill.isEmpty()) {
                node.fillType = SvgFillType::Solid;
                QColor c(fill);
                c.setAlphaF(attrs.hasAttribute("fill-opacity") ? attrs.value("fill-opacity").toDouble() : 1.0);
                node.fillColor = c;
            }

            QString stroke = attrs.value("stroke").toString();
            if (!stroke.isEmpty() && stroke != "none") {
                node.strokeEnabled = true;
                QColor c(stroke);
                c.setAlphaF(attrs.hasAttribute("stroke-opacity") ? attrs.value("stroke-opacity").toDouble() : 1.0);
                node.strokeColor = c;
                node.strokeWidth = attrs.value("stroke-width").toDouble();
                QString dash = attrs.value("stroke-dasharray").toString();
                if (!dash.isEmpty()) {
                    for (const QString &part : dash.split(","))
                        node.strokeDashPattern << part.toDouble();
                }
            }

            result.nodes << node;
        }
    }

    if (xml.hasError()) {
        if (errorOut) *errorOut = xml.errorString();
        return false;
    }

    doc = result;
    return true;
}

} // namespace SvgFileIO
