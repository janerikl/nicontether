#pragma once

#include <QWidget>
#include <QMap>

#include "camera/CameraSettings.h"

class QComboBox;
class QPushButton;
class QLabel;

// Right-hand dock: exposure/WB/quality combos plus AF and capture buttons.
// Combos are populated from the camera's reported config choices.
class ControlsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ControlsPanel(QWidget *parent = nullptr);

    void populate(const ConfigOptionMap &options);
    void setEnabledControls(bool enabled);
    // Reflect an externally-changed value without re-triggering setConfig.
    void updateValue(const QString &widgetName, const QString &value);

signals:
    // widgetName is the gphoto2 widget name; value is the chosen text.
    void configEditRequested(const QString &widgetName, const QString &value);
    void autofocusRequested();
    void captureRequested();

private:
    struct ControlRow {
        QString widgetName;
        QComboBox *combo = nullptr;
    };
    QComboBox *addRow(const QString &label);

    QMap<QString, ControlRow> m_rows; // logical key -> row
    QPushButton *m_afButton = nullptr;
    QPushButton *m_captureButton = nullptr;
    class QFormLayout *m_form = nullptr;
};
