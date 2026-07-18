#pragma once

#include <QString>

// Manages the on-disk capture session: a dated folder under a base directory,
// where downloaded NEF files are collected for the current shoot.
class SessionManager {
public:
    SessionManager();

    // Create/select a session named `name` (sanitized). Returns the folder path.
    QString startSession(const QString &name);

    QString currentDirectory() const { return m_dir; }
    QString baseDirectory() const { return m_base; }

private:
    QString m_base; // e.g. ~/Pictures/Tether
    QString m_dir;  // e.g. ~/Pictures/Tether/2026-07-17_studio
};
