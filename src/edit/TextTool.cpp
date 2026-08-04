#include "edit/TextTool.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetricsF>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {

// Simple separable box blur over an ARGB32_Premultiplied image's alpha
// channel, used to soften the drop-shadow mask. Self-contained rather than
// reusing Adjustments.cpp's boxBlur(), which operates on the 16-bit RGBA64
// working format used by the tone pipeline — this shadow mask is an 8-bit
// scratch layer local to text rendering.
QImage boxBlurAlpha(const QImage &src, int radius) {
    if (radius < 1) return src;
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_ARGB32_Premultiplied);
    QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);
    const int win = radius * 2 + 1;

    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *t = reinterpret_cast<QRgb *>(tmp.scanLine(y));
        long sa = 0;
        for (int x = -radius; x <= radius; ++x)
            sa += qAlpha(s[std::clamp(x, 0, w - 1)]);
        for (int x = 0; x < w; ++x) {
            int a = int(sa / win);
            t[x] = qRgba(0, 0, 0, a);
            sa -= qAlpha(s[std::clamp(x - radius, 0, w - 1)]);
            sa += qAlpha(s[std::clamp(x + radius + 1, 0, w - 1)]);
        }
    }
    for (int x = 0; x < w; ++x) {
        long sa = 0;
        for (int y = -radius; y <= radius; ++y)
            sa += qAlpha(reinterpret_cast<const QRgb *>(tmp.constScanLine(std::clamp(y, 0, h - 1)))[x]);
        for (int y = 0; y < h; ++y) {
            int a = int(sa / win);
            reinterpret_cast<QRgb *>(dst.scanLine(y))[x] = qRgba(0, 0, 0, a);
            sa -= qAlpha(reinterpret_cast<const QRgb *>(tmp.constScanLine(std::clamp(y - radius, 0, h - 1)))[x]);
            sa += qAlpha(reinterpret_cast<const QRgb *>(tmp.constScanLine(std::clamp(y + radius + 1, 0, h - 1)))[x]);
        }
    }
    return dst;
}

// Builds the glyph outline for (possibly multi-line) text, anchored so `pos`
// is the top-left of the text block, ascent-aligned per line.
QPainterPath buildTextPath(const TextOp &op) {
    QFont font(op.family);
    font.setPixelSize(std::max(1, int(std::lround(op.pixelSize))));
    font.setBold(op.bold);
    font.setItalic(op.italic);
    QFontMetricsF fm(font);

    QPainterPath path;
    const QStringList lines = op.text.split(QLatin1Char('\n'));
    double y = op.pos.y() + fm.ascent();
    for (const QString &line : lines) {
        if (!line.isEmpty()) path.addText(op.pos.x(), y, font, line);
        y += fm.lineSpacing();
    }
    return path;
}

} // namespace

QRectF textOpBounds(const TextOp &op) {
    QFont font(op.family);
    font.setPixelSize(std::max(1, int(std::lround(op.pixelSize))));
    font.setBold(op.bold);
    font.setItalic(op.italic);
    QFontMetricsF fm(font);

    const QStringList lines = op.text.isEmpty() ? QStringList{QString()}
                                                 : op.text.split(QLatin1Char('\n'));
    double maxWidth = 0.0;
    for (const QString &line : lines)
        maxWidth = std::max(maxWidth, double(fm.horizontalAdvance(line)));
    double height = fm.lineSpacing() * lines.size();
    return QRectF(op.pos.x(), op.pos.y(), std::max(1.0, maxWidth), std::max(1.0, height));
}

void applyTextOp(QImage &img, const TextOp &op) {
    if (op.text.trimmed().isEmpty()) return;

    QPainterPath path = buildTextPath(op);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.translate(op.pos);
    p.rotate(op.rotation);
    p.translate(-op.pos);

    if (op.bgEnabled) {
        QRectF bg = textOpBounds(op).adjusted(-op.bgPadding, -op.bgPadding,
                                              op.bgPadding, op.bgPadding);
        QColor c = op.bgColor;
        c.setAlphaF(std::clamp(op.bgOpacity, 0.0, 1.0));
        p.fillRect(bg, c);
    }

    if (op.shadowEnabled) {
        QRectF bounds = path.boundingRect().adjusted(-op.shadowBlur * 2, -op.shadowBlur * 2,
                                                       op.shadowBlur * 2, op.shadowBlur * 2);
        bounds = bounds.united(bounds.translated(op.shadowOffset));
        QRect ibounds = bounds.toAlignedRect();
        if (ibounds.width() > 0 && ibounds.height() > 0) {
            QImage mask(ibounds.size(), QImage::Format_ARGB32_Premultiplied);
            mask.fill(Qt::transparent);
            QPainter mp(&mask);
            mp.setRenderHint(QPainter::Antialiasing, true);
            mp.translate(-ibounds.topLeft());
            mp.fillPath(path, Qt::black);
            mp.end();
            mask = boxBlurAlpha(mask, std::max(1, int(std::lround(op.shadowBlur))));

            QImage tinted(mask.size(), QImage::Format_ARGB32_Premultiplied);
            tinted.fill(Qt::transparent);
            QPainter tp(&tinted);
            tp.setCompositionMode(QPainter::CompositionMode_Source);
            tp.drawImage(0, 0, mask);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            QColor tint = op.shadowColor;
            tint.setAlphaF(std::clamp(op.shadowOpacity, 0.0, 1.0));
            tp.fillRect(tinted.rect(), tint);
            tp.end();

            p.drawImage(ibounds.topLeft() + op.shadowOffset, tinted);
        }
    }

    p.setPen(Qt::NoPen);
    p.fillPath(path, op.color);

    if (op.outlineEnabled && op.outlineWidth > 0) {
        QPen pen(op.outlineColor, op.outlineWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        p.strokePath(path, pen);
    }
}
