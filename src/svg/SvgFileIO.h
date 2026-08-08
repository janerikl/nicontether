#pragma once

#include <QString>

#include "svg/SvgDocument.h"

// Saves/loads this editor's own SVG dialect via QXmlStreamWriter/Reader.
// `load()` only understands documents this editor wrote (or a close-enough
// hand match) — it is not a general-purpose SVG importer. Rasterizing
// arbitrary third-party SVGs (e.g. for Retouch's "Add SVG Layer…") goes
// through QSvgRenderer instead, see RetouchTab::addSvgLayer.
namespace SvgFileIO {
bool save(const SvgDocument &doc, const QString &path, QString *errorOut = nullptr);
bool load(SvgDocument &doc, const QString &path, QString *errorOut = nullptr);
} // namespace SvgFileIO
