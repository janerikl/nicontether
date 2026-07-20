#include "edit/NewDocumentDialog.h"
#include "edit/NewDocumentSize.h"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>

NewDocumentDialog::NewDocumentDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("New Document");

    m_width = new QDoubleSpinBox;
    m_width->setRange(1, 100000);
    m_width->setValue(1920);

    m_height = new QDoubleSpinBox;
    m_height->setRange(1, 100000);
    m_height->setValue(1080);

    m_unit = new QComboBox;
    m_unit->addItem("Pixels", int(SizeUnit::Pixels));
    m_unit->addItem("Inches", int(SizeUnit::Inches));
    m_unit->addItem("Centimeters", int(SizeUnit::Centimeters));

    m_dpi = new QDoubleSpinBox;
    m_dpi->setRange(1, 2400);
    m_dpi->setValue(300);

    auto *form = new QFormLayout;
    form->addRow("Width:", m_width);
    form->addRow("Height:", m_height);
    form->addRow("Unit:", m_unit);
    form->addRow("Resolution (DPI):", m_dpi);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewDocumentDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void NewDocumentDialog::onAccept() {
    SizeUnit unit = SizeUnit(m_unit->currentData().toInt());
    m_result = computeCanvasPixelSize(m_width->value(), m_height->value(), unit, m_dpi->value());
    accept();
}
