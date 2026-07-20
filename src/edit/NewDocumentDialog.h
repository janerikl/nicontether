#pragma once

#include <QDialog>
#include <QSize>

class QDoubleSpinBox;
class QComboBox;

// File > New: prompts for width/height/unit/DPI, exposes the resolved pixel
// size via resultPixelSize() after exec() returns QDialog::Accepted.
class NewDocumentDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDocumentDialog(QWidget *parent = nullptr);
    QSize resultPixelSize() const { return m_result; }

private slots:
    void onAccept();

private:
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QComboBox *m_unit = nullptr;
    QDoubleSpinBox *m_dpi = nullptr;
    QSize m_result;
};
