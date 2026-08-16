#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <cstdint>

// One browsable image file found on the device (DCIM/... tree).
struct MtpEntry {
    uint32_t id = 0;
    QString name;
    uint64_t size = 0;
};
Q_DECLARE_METATYPE(MtpEntry)
Q_DECLARE_METATYPE(QVector<MtpEntry>)
