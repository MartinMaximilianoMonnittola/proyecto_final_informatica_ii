#include "jointbar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QFont>
#include <algorithm>

JointBar::JointBar(int index, const QString &nombre, const QString &modelo, int gpio,
                   QWidget *parent)
    : QWidget(parent), m_index(index)
{
    setObjectName("jointBar");
    setStyleSheet(R"(
        QWidget#jointBar { background: #111827; border: 1px solid #25344c; }
        QLabel { color: white; background: transparent; font-family: 'Segoe UI'; }
        QSpinBox { background: #e9f0f7; color: #111827; border: 1px solid #91a3b8;
                   border-radius: 3px; font-size: 18px; font-weight: bold; padding: 2px; }
        QSlider::groove:horizontal { height: 9px; background: #46556a; border-radius: 4px; }
        QSlider::sub-page:horizontal { background: #1ca1c6; border-radius: 4px; }
        QSlider::handle:horizontal { background: white; width: 20px; margin: -6px 0;
                                     border: 2px solid #1ca1c6; border-radius: 10px; }
    )");
    auto *fila = new QHBoxLayout(this);
    fila->setContentsMargins(12, 5, 12, 5);
    fila->setSpacing(12);
    auto *etiqueta = new QLabel(QString("S%1  %2\n%3 / GPIO %4")
                                .arg(index).arg(nombre).arg(modelo).arg(gpio), this);
    etiqueta->setMinimumWidth(190);
    etiqueta->setMaximumWidth(190);
    etiqueta->setStyleSheet("font-size: 14px; font-weight: bold; color: #f6faff; background: transparent;");
    fila->addWidget(etiqueta);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(20,160);
    m_slider->setValue(90);
    fila->addWidget(m_slider,1);
    m_spin = new QSpinBox(this);
    m_spin->setRange(20,160);
    m_spin->setValue(90);
    m_spin->setSuffix(" °");
    m_spin->setFixedWidth(132); // Evita recortar 160/180 grados + sufijo y flechas.
    fila->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        QSignalBlocker noReaccion(m_spin);
        m_spin->setValue(v);
        emit angleChanged(m_index, v);
    });
    connect(m_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        QSignalBlocker noReaccion(m_slider);
        m_slider->setValue(v);
        emit angleChanged(m_index, v);
    });
}

int JointBar::value() const { return m_spin->value(); }
int JointBar::minimum() const { return m_spin->minimum(); }
int JointBar::maximum() const { return m_spin->maximum(); }

void JointBar::setValue(int v) {
    QSignalBlocker b1(m_slider), b2(m_spin);
    m_slider->setValue(v);
    m_spin->setValue(v);
}
void JointBar::setLimits(int minimo, int maximo) {
    if (minimo >= maximo) return;
    QSignalBlocker b1(m_slider), b2(m_spin);
    m_slider->setRange(minimo,maximo);
    m_spin->setRange(minimo,maximo);
}
