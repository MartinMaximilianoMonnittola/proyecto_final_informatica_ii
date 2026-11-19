#pragma once
#include <QWidget>
#include <QString>

class QSlider;
class QSpinBox;

class JointBar : public QWidget {
    Q_OBJECT
public:
    JointBar(int index, const QString &nombre, const QString &modelo, int gpio,
             QWidget *parent = nullptr);
    int value() const;
    int minimum() const;
    int maximum() const;
    void setValue(int value);
    void setLimits(int minimo, int maximo);
signals:
    void angleChanged(int index, int angle);
private:
    int m_index;
    QSlider *m_slider = nullptr;
    QSpinBox *m_spin = nullptr;
};
