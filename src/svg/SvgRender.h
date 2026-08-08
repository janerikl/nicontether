#pragma once

#include <QImage>
#include <QPainterPath>
#include <QTransform>

#include "svg/SvgDocument.h"

// Shared rendering helpers used by SvgCanvas (interactive editing), PNG
// export, and Retouch's SVG-layer rasterization — kept here so all three
// draw nodes identically.
QPainterPath svgNodeLocalPath(const SvgNode &node);
QTransform svgNodeTransform(const SvgNode &node);
QPainterPath svgNodeWorldPath(const SvgNode &node);

// Paints every visible, non-group node of `doc` onto `painter` at (0,0) in
// document coordinates — caller is responsible for any translate/scale.
void renderSvgDocument(class QPainter &painter, const SvgDocument &doc);

// Rasterizes the full document canvas at `scale`x resolution (e.g. 2-4 for
// crisp import into a raster layer).
QImage renderSvgDocumentToImage(const SvgDocument &doc, qreal scale = 1.0);
