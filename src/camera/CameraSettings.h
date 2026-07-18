#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QMetaType>

// A single camera configuration control (maps to one gphoto2 widget).
struct ConfigOption {
    QString key;        // logical key: "shutterspeed", "aperture", "iso", ...
    QString widgetName; // actual gphoto2 widget name on this camera
    QString label;      // human-friendly label
    QString current;    // current value as text
    QStringList choices;// available radio/menu choices (empty for text/toggle)
    bool readOnly = false;
};

// Map of logical key -> ConfigOption, emitted when a camera connects.
using ConfigOptionMap = QMap<QString, ConfigOption>;

Q_DECLARE_METATYPE(ConfigOption)
Q_DECLARE_METATYPE(ConfigOptionMap)
