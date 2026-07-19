#include "ui/PreferencesDialog.h"

#include "camera/CameraModels.h"

#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>

void afFrameForModel(const QString &id, int &w, int &h) {
    QSettings s;
    const cammodel::Model *m = cammodel::byId(id.toStdString());
    int dw = m ? m->afFrameW : 640;
    int dh = m ? m->afFrameH : 426;
    w = s.value(QString("af/models/%1/frameWidth").arg(id), dw).toInt();
    h = s.value(QString("af/models/%1/frameHeight").arg(id), dh).toInt();
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");

    auto *outer = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_model = new QComboBox;
    for (const cammodel::Model &m : cammodel::models())
        m_model->addItem(m.display, QString::fromLatin1(m.id));
    form->addRow("Camera model:", m_model);

    m_frameW = new QSpinBox;
    m_frameH = new QSpinBox;
    m_frameW->setRange(1, 20000);
    m_frameH->setRange(1, 20000);
    form->addRow("AF frame width:", m_frameW);
    form->addRow("AF frame height:", m_frameH);

    m_calibrate = new QPushButton("Calibrate…");
    form->addRow(QString(), m_calibrate);
    connect(m_calibrate, &QPushButton::clicked, this,
            [this] { emit calibrationRequested(); });

    auto *hint = new QLabel(
        "Click-to-focus calibration. Click Calibrate…, then click the point "
        "you want in focus, then click where it actually snapped sharp -- "
        "the AF frame size is solved automatically from those two clicks. "
        "Values are remembered per model.");
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(buttons);

    // Restore the last-used model.
    QSettings s;
    QString cur = s.value("af/currentModel", "custom").toString();
    int idx = m_model->findData(cur);
    if (idx < 0) idx = m_model->findData("custom");
    {
        QSignalBlocker b(m_model);
        m_model->setCurrentIndex(idx);
    }
    loadFrameForCurrentModel();

    connect(m_model, &QComboBox::currentIndexChanged, this,
            [this](int) { onModelChanged(); });
    connect(m_frameW, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
    connect(m_frameH, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { onFrameEdited(); });
}

QString PreferencesDialog::currentModelId() const {
    return m_model->currentData().toString();
}

void PreferencesDialog::loadFrameForCurrentModel() {
    int w = 0, h = 0;
    afFrameForModel(currentModelId(), w, h);
    QSignalBlocker bw(m_frameW);
    QSignalBlocker bh(m_frameH);
    m_frameW->setValue(w);
    m_frameH->setValue(h);
}

void PreferencesDialog::onModelChanged() {
    QSettings s;
    s.setValue("af/currentModel", currentModelId());
    loadFrameForCurrentModel();
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::onFrameEdited() {
    QSettings s;
    const QString id = currentModelId();
    s.setValue(QString("af/models/%1/frameWidth").arg(id), m_frameW->value());
    s.setValue(QString("af/models/%1/frameHeight").arg(id), m_frameH->value());
    emit afFrameSizeChanged(m_frameW->value(), m_frameH->value());
}

void PreferencesDialog::selectModelById(const QString &id) {
    if (id.isEmpty()) return;
    int idx = m_model->findData(id);
    if (idx < 0 || idx == m_model->currentIndex()) return;
    m_model->setCurrentIndex(idx); // triggers onModelChanged()
}

void PreferencesDialog::setAfFrame(int w, int h) {
    {
        QSignalBlocker bw(m_frameW);
        QSignalBlocker bh(m_frameH);
        m_frameW->setValue(w);
        m_frameH->setValue(h);
    }
    onFrameEdited(); // persist for the current model + emit afFrameSizeChanged
}
