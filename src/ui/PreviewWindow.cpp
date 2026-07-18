#include "ui/PreviewWindow.h"
#include "ui/PannableScrollArea.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QtMath>

namespace {
constexpr double kMinZoom = 0.10; //  10%
constexpr double kMaxZoom = 4.00; // 400%
}

PreviewWindow::PreviewWindow(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *topRow = new QHBoxLayout;
    m_fitButton = new QPushButton("Fit");
    m_oneToOneButton = new QPushButton("1:1");
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(int(kMinZoom * 100), int(kMaxZoom * 100));
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(180);
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(48);
    m_titleLabel = new QLabel;

    topRow->addWidget(m_fitButton);
    topRow->addWidget(m_oneToOneButton);
    topRow->addWidget(m_zoomSlider);
    topRow->addWidget(m_zoomLabel);
    topRow->addWidget(m_titleLabel, 1, Qt::AlignRight);
    layout->addLayout(topRow);

    m_scroll = new PannableScrollArea;
    m_scroll->setWidgetResizable(false);
    m_scroll->setAlignment(Qt::AlignCenter); // center image when smaller than viewport
    m_imageLabel = new QLabel;
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_scroll->setWidget(m_imageLabel);
    layout->addWidget(m_scroll, 1);

    connect(m_fitButton, &QPushButton::clicked, this, [this] {
        m_fit = true;
        applyScaling();
        syncSlider();
        m_scroll->setFocus();
    });
    connect(m_oneToOneButton, &QPushButton::clicked, this, [this] {
        setZoomAnchored(1.0, m_scroll->viewport()->rect().center());
        m_scroll->setFocus();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PreviewWindow::onSliderChanged);
    connect(m_scroll, &PannableScrollArea::zoomStep, this, &PreviewWindow::onWheelZoom);
}

void PreviewWindow::showImage(const QString &path, const QImage &image) {
    m_image = image;
    m_titleLabel->setText(QFileInfo(path).fileName());
    m_fit = true; // new frames start fitted
    applyScaling();
    syncSlider();
}

void PreviewWindow::focusView() {
    m_scroll->setFocus();
}

double PreviewWindow::fitFactor() const {
    if (m_image.isNull()) return 1.0;
    QSize vp = m_scroll->viewport()->size();
    double fx = double(vp.width()) / m_image.width();
    double fy = double(vp.height()) / m_image.height();
    return qMin(fx, fy);
}

double PreviewWindow::currentFactor() const {
    return m_fit ? fitFactor() : m_zoom;
}

void PreviewWindow::applyScaling() {
    if (m_image.isNull()) {
        m_imageLabel->setText("No preview available");
        m_imageLabel->adjustSize();
        return;
    }
    double f = currentFactor();
    QSize target(qMax(1, int(m_image.width() * f)),
                 qMax(1, int(m_image.height() * f)));
    QPixmap pix = QPixmap::fromImage(m_image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(pix);
    m_imageLabel->resize(pix.size());
}

void PreviewWindow::setZoomAnchored(double newZoom, const QPoint &anchor) {
    newZoom = qBound(kMinZoom, newZoom, kMaxZoom);

    // Content point currently under the anchor, in image pixels.
    double oldF = currentFactor();
    double hval = m_scroll->horizontalScrollBar()->value();
    double vval = m_scroll->verticalScrollBar()->value();
    double imgX = (hval + anchor.x()) / oldF;
    double imgY = (vval + anchor.y()) / oldF;

    m_fit = false;
    m_zoom = newZoom;
    applyScaling();

    // Restore that image point under the anchor after rescaling.
    m_scroll->horizontalScrollBar()->setValue(int(imgX * newZoom - anchor.x()));
    m_scroll->verticalScrollBar()->setValue(int(imgY * newZoom - anchor.y()));

    syncSlider();
}

void PreviewWindow::onWheelZoom(int steps, const QPoint &anchor) {
    double base = currentFactor();
    double factor = qPow(1.25, steps); // 25% per notch
    setZoomAnchored(base * factor, anchor);
}

void PreviewWindow::onSliderChanged(int percent) {
    setZoomAnchored(percent / 100.0, m_scroll->viewport()->rect().center());
}

void PreviewWindow::syncSlider() {
    double f = currentFactor();
    int percent = qBound(m_zoomSlider->minimum(),
                         int(qRound(f * 100.0)),
                         m_zoomSlider->maximum());
    QSignalBlocker block(m_zoomSlider);
    m_zoomSlider->setValue(percent);
    m_zoomLabel->setText(QString::number(int(qRound(f * 100.0))) + "%");
}
