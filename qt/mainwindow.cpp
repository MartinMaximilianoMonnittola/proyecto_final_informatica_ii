#include "mainwindow.h"
#include "jointbar.h"
#include "ambient_classification.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QGraphicsSimpleTextItem>
#include <QCheckBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QGroupBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QTextEdit>
#include <QTextCursor>
#include <QFileDialog>
#include <QFont>
#include <QGraphicsProxyWidget>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QPen>
#include <QBrush>
#include <QFrame>
#include <QResizeEvent>
#include <QShowEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QStringList>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// IMPORTANTE: la vista debe recalcular el zoom cuando cambia su propio viewport.
// Hacer fitInView en MainWindow::resizeEvent toma a veces el ancho anterior
// (por ejemplo ~640px), dejando el panel diminuto dentro de una ventana grande.
class AutoFitGraphicsView : public QGraphicsView {
public:
    explicit AutoFitGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent) {}
protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        ajustarEscala();
    }
    void showEvent(QShowEvent *event) override {
        QGraphicsView::showEvent(event);
        ajustarEscala();
    }
private:
    void ajustarEscala() {
        if (scene() && viewport() && viewport()->width() > 0 && viewport()->height() > 0) {
            setAlignment(Qt::AlignCenter);
            fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        }
    }
};

namespace {
constexpr int ServoCount = 6;
constexpr int BAUD = 115200;
const QString ServoNames[ServoCount] = {"PINZA", "MUNECA 1", "MUNECA 2", "CODO", "HOMBRO", "BASE"};
const QString ServoModels[ServoCount] = {"MG90S", "MG90S", "MG90S", "MG996R", "MG996R", "MG996R"};
const int ServoPins[ServoCount] = {12, 14, 27, 26, 25, 33};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Proyecto final Informatica 2 | Brazo robotico ESP32 + DHT22 | RANGOS HR+T v3");
    resize(1536, 864);
    setMinimumSize(1024, 620);

    m_serial.setBaudRate(BAUD);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);
    connect(&m_serial, &QSerialPort::readyRead, this, &MainWindow::processInput);
    connect(&m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) return;
        if (error == QSerialPort::ResourceError) {
            const QString msg = m_serial.errorString();
            disconnectSerial();
            setStatus("Error USB / puerto desconectado: " + msg);
        }
    });

    buildUi();
    buildAutoUi();
    m_autoStepTimer.setSingleShot(true);
    connect(&m_autoStepTimer, &QTimer::timeout, this, &MainWindow::sendAutoStep);
    m_autoMeasureTimer.setInterval(250);
    connect(&m_autoMeasureTimer, &QTimer::timeout, this, &MainWindow::updateAutoMeasurement);
    m_autoNextCycleTimer.setSingleShot(true);
    connect(&m_autoNextCycleTimer, &QTimer::timeout, this, [this] {
        if (m_autoRunning && !m_autoTestMode && !m_autoMeasurementOnly)
            startAutoStage(0);
    });
    m_dhtTimeout.setSingleShot(true);
    m_dhtTimeout.setInterval(8000);
    connect(&m_dhtTimeout, &QTimer::timeout, this, [this] {
        setDhtSinDatos("SIN DATOS: sensor no responde");
        if (m_autoRunning && m_autoState == AutoState::Measuring)
            stopAuto(true, "Medicion cancelada: sin telemetria DHT22.");
    });
    // F11: alternar pantalla completa (Esc para regresar a ventana normal).
    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullScreenShortcut, &QShortcut::activated, this, [this] {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });
    auto *exitFullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(exitFullScreenShortcut, &QShortcut::activated, this, [this] {
        if (isFullScreen()) showNormal();
    });
    for (int i = 0; i < ServoCount; ++i) {
        m_jointDebounce[i].setSingleShot(true);
        m_jointDebounce[i].setInterval(60);
        connect(&m_jointDebounce[i], &QTimer::timeout, this, [this, i] { sendSingleJoint(i); });
    }
    connect(&m_playbackTimer, &QTimer::timeout, this, &MainWindow::playNext);
    auto *emergencyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space), this);
    connect(emergencyShortcut, &QShortcut::activated, this, [this] {
        if (m_autoRunning) stopAuto(true, "Ciclo detenido por el usuario (Ctrl+Espacio).");
        else stopPlayback(true);
    });
    // Ctrl+L muestra el historial TX/RX sin abrir el puerto en otra aplicacion.
    auto *diagnosticShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(diagnosticShortcut, &QShortcut::activated, this, &MainWindow::showSerialDiagnostic);
    refreshPorts();
    setStatus("Elegí el puerto COM, conectá el ESP32 y probá los ejes sin carga.");
}

QWidget *MainWindow::place(QWidget *widget, int x, int y, int w, int h) {
    widget->setFixedSize(w, h);
    QGraphicsProxyWidget *proxy = m_scene->addWidget(widget);
    proxy->setPos(x, y);
    // El popup de QComboBox se incorpora como hijo del proxy dentro
    // de QGraphicsScene. Si queda a la misma profundidad (z=2) que los
    // botones, el menu aparece DETRAS de ellos en Windows.
    // Elevar el proxy completo asegura que su popup se pinte por delante.
    proxy->setZValue(qobject_cast<QComboBox *>(widget) ? 100.0 : 2.0);
    return widget;
}

QPushButton *MainWindow::makeButton(const QString &caption, int x, int y, int w, int h,
                                    const QString &objectName) {
    auto *button = new QPushButton(caption);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    place(button, x, y, w, h);
    return button;
}

QLabel *MainWindow::makeLabel(const QString &caption, int x, int y, int w, int h,
                              const QString &objectName) {
    auto *label = new QLabel(caption);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    place(label, x, y, w, h);
    return label;
}

void MainWindow::buildUi() {
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0, 0, 1920, 1080);
    // Fondo dibujado por Qt: no necesita imagenes o recursos externos.
    // Tres columnas, con area ampliada para la lista de puertos serie.
    m_scene->setBackgroundBrush(QBrush(QColor("#edf2f7")));
    m_scene->addRect(QRectF(0, 0, 1920, 122), QPen(Qt::NoPen), QBrush(Qt::white))->setZValue(-10);
    m_scene->addRect(QRectF(0, 0, 1920, 8), QPen(Qt::NoPen), QBrush(QColor("#111827")))->setZValue(-9);
    m_scene->addRect(QRectF(43, 32, 9, 66), QPen(Qt::NoPen), QBrush(QColor("#157ea2")))->setZValue(-9);
    QFont tituloFuente("Segoe UI", 48, QFont::DemiBold);
    auto *titulo = m_scene->addSimpleText("Proyecto final Informatica 2", tituloFuente);
    titulo->setPos(73, 23);
    titulo->setBrush(QBrush(QColor("#101827")));
    titulo->setZValue(-8);
    QFont subFuente("Segoe UI", 15);
    auto *sub = m_scene->addSimpleText("BRAZO ROBOTICO  /  ESP32  /  QT  /  DHT22", subFuente);
    sub->setPos(1290, 73);
    sub->setBrush(QBrush(QColor("#57687c")));
    sub->setZValue(-8);
    m_scene->addRect(QRectF(39, 140, 714, 907), QPen(QColor("#d7e0e9"), 2), QBrush(Qt::white))->setZValue(-5);
    m_scene->addRect(QRectF(771, 269, 733, 783), QPen(QColor("#d7e0e9"), 2), QBrush(Qt::white))->setZValue(-5);
    m_scene->addRect(QRectF(1525, 140, 370, 907), QPen(QColor("#d7e0e9"), 2), QBrush(Qt::white))->setZValue(-5);
    for (int i=0; i<6; ++i) {
        m_scene->addRect(QRectF(788, 263+i*118, 696, 79),
                         QPen(QColor("#e3eaf2"), 1), QBrush(QColor("#f8fafc")))->setZValue(-4);
    }

    m_view = new AutoFitGraphicsView(m_scene);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setStyleSheet("background: #edf2f7; border: 0;");
    m_view->setRenderHint(QPainter::Antialiasing, true);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    // Dos pestañas evitan achicar la interfaz manual al agregar las 4 etapas.
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("mainTabs");
    m_tabs->setStyleSheet(R"(
      QTabWidget::pane { border: 0; background: #edf2f7; }
      QTabBar::tab { background: #d7e0ec; color: #122035; font-size: 16px;
                      font-weight: bold; padding: 10px 25px; margin-right: 3px; }
      QTabBar::tab:selected { background: #ffffff; border-bottom: 3px solid #1788ad; }
    )");
    m_tabs->addTab(m_view, "CONTROL MANUAL / SECUENCIAS");
    setCentralWidget(m_tabs);

    const QString css = R"(
        QLabel#info {font-family: 'Segoe UI'; font-size: 18px; color: #101820; font-weight: 600; background: transparent;}
        QLabel#section {font-family: 'Segoe UI'; font-size: 18px; color: #121927; font-weight: bold; background: transparent;}
        QLabel#status {font-family: 'Segoe UI'; font-size: 17px; color: #22314a; background: #f0f4f8; border: 1px solid #cdd6df; padding: 10px; border-radius: 5px;}
        QLabel#connection {font-family: 'Segoe UI'; font-size: 16px; color: #8a491e; font-weight: bold; background: transparent;}
        QLabel#dhtTitle {font-family: 'Segoe UI'; font-size: 19px; font-weight: bold;
             color: #14243b; background: transparent;}
        QLabel#dhtValue {font-family: 'Segoe UI'; font-size: 21px; font-weight: bold;
             color: #14243b; background: transparent;}
        QLabel#dhtState {font-family: 'Segoe UI'; font-size: 19px; font-weight: bold;
             color: #42526a; background: transparent;}
        QLabel#count {font-family: 'Segoe UI'; font-size: 16px; color: #24334a; font-weight: bold; background: transparent;}
        QPushButton#action {background: #05070b; color: white; font-family: 'Segoe UI'; font-weight: bold; font-size: 23px; border: 1px solid #05070b; padding: 6px;}
        QPushButton#action:hover {background: #22324b;}
        QPushButton#action:pressed {background: #0f577e;}
        QPushButton#stop {background: #6a121e; color: white; font-family: 'Segoe UI'; font-weight: bold; font-size: 23px; border: none;}
        QPushButton#stop:hover {background: #b12030;}
        QPushButton#small {background: #101520; color: white; font-family: 'Segoe UI'; font-size: 17px; font-weight: bold; border: 0; padding: 5px;}
        QPushButton#small:hover {background: #284267;}
        QPushButton#outline {background: white; color: #121827; font-family: 'Segoe UI'; font-size: 16px; font-weight: 600; border: 2px solid #1c2734;}
        QPushButton#outline:hover {background: #e7eff8;}
        QComboBox, QSpinBox {background: #11151c; color: white; border: 1px solid #1b2736; font-family: 'Segoe UI'; font-size: 19px; padding: 5px;}
        QComboBox QAbstractItemView {background: white; color: #121827; selection-background-color: #cedded;}
        QCheckBox {color: #142133; font-family: 'Segoe UI'; font-size: 19px; font-weight: bold; background: transparent; spacing: 12px;}
        QListWidget {background: #f4f6fa; color: #162235; font-family: Consolas, monospace; font-size: 16px; border: 2px solid #101820; padding: 4px;}
        QListWidget::item:selected {background: #20344d; color: white;}
    )";
    qApp->setStyleSheet(css);

    // Parte superior izquierda: puerto, conexion y protocolo.
    makeLabel("CONTROL USB / ESP32", 60, 162, 660, 30, "section");
    m_ports = new QComboBox;
    m_ports->setToolTip("Puerto serie del ESP32 a 115200, 8N1");
    m_ports->setMaxVisibleItems(15);
    m_ports->setMinimumContentsLength(38);
    place(m_ports, 60, 202, 524, 49); // Antes: 251 px. Ahora se lee la descripcion.
    // Mismo ancho que el combo, para no tapar innecesariamente ACTUALIZAR.
    m_ports->view()->setMinimumWidth(524);
    auto *refresh = makeButton("ACTUALIZAR", 595, 202, 148, 49, "small");
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    m_connect = makeButton("CONECTAR ESP32", 60, 268, 318, 53, "small");
    connect(m_connect, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    m_connectionState = makeLabel("● DESCONECTADO", 396, 272, 344, 42, "connection");

    // Panel de telemetria: libre en la franja central superior del fondo.
    // Esta lectura se refiere al aire cerca de la garra, no al contenido del farmaco.
    m_scene->addRect(QRectF(780, 101, 716, 155), QPen(QColor("#cbd6e1"), 2),
                     QBrush(QColor("#f8fafc")))->setZValue(1);
    makeLabel("SENSOR AMBIENTAL EN LA GARRA (DHT22)",
              801, 109, 675, 34, "dhtTitle");
    m_dhtTemperatura = makeLabel("TEMPERATURA: --.- °C",
                                  802, 146, 330, 36, "dhtValue");
    m_dhtHumedad = makeLabel("HUMEDAD: --.- %",
                              1140, 146, 337, 36, "dhtValue");
    m_dhtEstado = makeLabel("SIN DATOS: ESP32 desconectado",
                             802, 189, 674, 46, "dhtState");

    // Seis barras ubicadas sobre los seis rectangulos negros de la imagen de Processing.
    for (int i = 0; i < ServoCount; ++i) {
        auto *bar = new JointBar(i, ServoNames[i], ServoModels[i], ServoPins[i]);
        place(bar, 798, 273 + i*118, 676, 58);
        connect(bar, &JointBar::angleChanged, this, [this](int index, int angle) {
            Q_UNUSED(angle)
            if (m_receivingState) return;
            if (m_playing) stopPlayback(false);
            m_jointDebounce[index].start();
            setStatus(QString("Eje %1: consigna %2 grados (NO es medicion de posicion).")
                      .arg(index + 1).arg(m_joints[index]->value()));
        });
        m_joints.append(bar);
    }

    // Tres botones a la izquierda como el programa original.
    connect(makeButton("GUARDAR\nPOSICIONES", 58, 327, 344, 102),
            &QPushButton::clicked, this, &MainWindow::recordPose);
    connect(makeButton("REPRODUCIR\nMOVIMIENTOS", 58, 489, 344, 102),
            &QPushButton::clicked, this, &MainWindow::startPlayback);
    connect(makeButton("DETENER\nMOVIMIENTO", 58, 651, 344, 102, "stop"),
            &QPushButton::clicked, this, [this] { stopPlayback(true); });

    // Tres botones de importacion, exportacion y centrado a la derecha.
    connect(makeButton("EXPORTAR\nPOSICIONES", 1536, 327, 344, 102),
            &QPushButton::clicked, this, &MainWindow::exportSequence);
    connect(makeButton("IMPORTAR\nPOSICIONES", 1536, 489, 344, 102),
            &QPushButton::clicked, this, &MainWindow::importSequence);
    connect(makeButton("RESETEAR\nPOSICIONES", 1536, 651, 344, 102),
            &QPushButton::clicked, this, &MainWindow::homePose);

    // Opciones nuevas que conservan las funciones del proyecto anterior.
    makeLabel("TIEMPO POR PASO (ms)", 58, 775, 347, 30, "section");
    m_duration = new QSpinBox;
    m_duration->setRange(100, 10000);
    m_duration->setSingleStep(100);
    m_duration->setValue(1000);
    m_duration->setSuffix(" ms");
    place(m_duration, 55, 808, 350, 58);
    m_repeat = new QCheckBox("REPETIR SECUENCIA");
    place(m_repeat, 58, 891, 344, 42);
    connect(makeButton("CONFIGURAR LIMITES DE EJES", 58, 962, 344, 51, "outline"),
            &QPushButton::clicked, this, &MainWindow::openLimitsDialog);

    makeLabel("FIRMWARE / PROTOCOLO", 1536, 179, 354, 30, "section");
    m_protocol = new QComboBox;
    m_protocol->addItem("Info2: SET / MOVE");
    m_protocol->addItem("Anterior: J / A");
    m_protocol->setToolTip("Elegir el protocolo que entiende el sketch ESP32 cargado.");
    place(m_protocol, 1536, 214, 344, 52);

    makeLabel("SECUENCIA GRABADA", 1536, 770, 344, 28, "section");
    m_records = new QListWidget;
    m_records->setToolTip("Doble clic para ejecutar la postura seleccionada.");
    place(m_records, 1536, 805, 344, 164);
    connect(m_records, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const int row = m_records->row(item);
        if (row < 0 || row >= m_poses.size()) return;
        stopPlayback(false);
        setPose(m_poses[row]);
        sendFullPose(m_poses[row], m_duration->value());
        setStatus(QString("Enviada postura grabada %1.").arg(row+1));
    });
    connect(makeButton("BORRAR SECUENCIA", 1536, 979, 344, 49, "outline"),
            &QPushButton::clicked, this, &MainWindow::clearSequence);

    m_sequenceCount = makeLabel("0 POSICIONES GUARDADAS", 798, 935, 676, 30, "count");
    makeLabel("Angulos ordenados, no mediciones reales de posicion.",
              798, 966, 676, 26, "info");
    m_status = makeLabel("", 798, 996, 676, 53, "status");
    updateRecordList();
}

MainWindow::Protocol MainWindow::protocol() const {
    return m_protocol->currentIndex() == 1 ? Protocol::Legacy : Protocol::Info2;
}

void MainWindow::refreshPorts() {
    const QString previous = m_ports->currentData().toString();
    m_ports->clear();
    auto ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
        return a.portName() < b.portName();
    });
    for (const QSerialPortInfo &port : ports) {
        QString title = port.portName();
        if (!port.description().trimmed().isEmpty()) title += " - " + port.description();
        m_ports->addItem(title, port.portName());
    }
    if (m_ports->count() == 0) m_ports->addItem("NO SE ENCONTRARON PUERTOS", QString());
    for (int i=0; i<m_ports->count(); ++i)
        m_ports->setItemData(i, m_ports->itemText(i), Qt::ToolTipRole);
    const int previousIndex = m_ports->findData(previous);
    if (previousIndex >= 0) m_ports->setCurrentIndex(previousIndex);
    setStatus(QString("Puertos detectados: %1. Elegi el puerto del ESP32.").arg(ports.size()));
}

void MainWindow::toggleConnection() {
    if (m_serial.isOpen()) { disconnectSerial(); setStatus("ESP32 desconectado."); return; }
    const QString port = m_ports->currentData().toString();
    if (port.isEmpty()) {
        QMessageBox::information(this, "Sin puertos", "No se encontro un puerto COM. Conecta el ESP32 por USB y actualiza.");
        return;
    }
    m_serial.setPortName(port);
    if (!m_serial.open(QIODevice::ReadWrite)) {
        QMessageBox::critical(this, "Error de conexion", "No se pudo abrir " + port + ": " + m_serial.errorString());
        return;
    }
    // Confirmar la configuracion en el dispositivo ya abierto; Qt puede
    // aplicarla antes de open(), pero aqui detectamos explicitamente errores.
    if (!m_serial.setBaudRate(BAUD) ||
        !m_serial.setDataBits(QSerialPort::Data8) ||
        !m_serial.setParity(QSerialPort::NoParity) ||
        !m_serial.setStopBits(QSerialPort::OneStop) ||
        !m_serial.setFlowControl(QSerialPort::NoFlowControl)) {
        const QString reason = m_serial.errorString();
        m_serial.close();
        QMessageBox::critical(this, "Configuracion serial", "No se pudo configurar el puerto a 115200, 8N1: " + reason);
        return;
    }
    logSerial("INFO", QString("Parametros del puerto: %1 baudios, 8N1, sin control de flujo")
              .arg(m_serial.baudRate()));
    m_inputBuffer.clear();
    m_discardingSerialLine = false;
    setDhtSinDatos("ESPERANDO LECTURA DHT22...");
    m_dhtTimeout.start();
    m_connect->setText("DESCONECTAR");
    m_connectionState->setText("● CONECTADO " + port);
    m_connectionState->setStyleSheet("color: #14723c;");
    m_ports->setEnabled(false);
    m_protocol->setEnabled(false);
    logSerial("INFO", "Puerto abierto: " + port + "; protocolo=" +
              (protocol() == Protocol::Info2 ? QString("Info2") : QString("Legacy")));
    // El conversor CP2102 puede provocar un reset del ESP32 al abrir el puerto.
    // Un breve retardo evita consultar durante el arranque.
    QTimer::singleShot(1800, this, [this, port] {
        if (!m_serial.isOpen() || m_serial.portName() != port) return;
        sendLine("PING");
        sendLine("GET");
    });
    setStatus("Puerto abierto " + port + " a 115200; Ctrl+L: diagnostico TX/RX.");
}

void MainWindow::disconnectSerial() {
    if (m_autoRunning) stopAuto(false, "Ciclo cancelado: ESP32 desconectado.");
    stopPlayback(false);
    for (auto &timer : m_jointDebounce) timer.stop();
    m_inputBuffer.clear();
    m_discardingSerialLine = false;
    if (m_serial.isOpen()) { logSerial("INFO", "Puerto cerrado"); m_serial.close(); }
    m_dhtTimeout.stop();
    setDhtSinDatos("SIN DATOS: ESP32 desconectado");
    m_connect->setText("CONECTAR ESP32");
    m_connectionState->setText("● DESCONECTADO");
    m_connectionState->setStyleSheet("color: #8a491e;");
    m_ports->setEnabled(true);
    m_protocol->setEnabled(true);
}

bool MainWindow::sendLine(const QString &line) {
    if (!m_serial.isOpen()) return false;
    QByteArray data = line.toUtf8();
    data.append('\n');
    const qint64 escritos = m_serial.write(data);
    m_lastCommand = line;
    logSerial("TX", line);
    if (escritos != data.size()) {
        logSerial("ERROR", "No se pudo escribir el comando completo: " + m_serial.errorString());
        return false;
    }
    return true;
}

QVector<int> MainWindow::currentPose() const {
    QVector<int> result;
    for (JointBar *bar : m_joints) result.append(bar->value());
    return result;
}

void MainWindow::setPose(const QVector<int> &pose) {
    if (pose.size() != ServoCount) return;
    for (int i = 0; i < ServoCount; ++i) {
        m_jointDebounce[i].stop();
        m_joints[i]->setValue(pose[i]);
    }
}

void MainWindow::sendSingleJoint(int index) {
    if (m_autoRunning || !m_serial.isOpen() || index < 0 || index >= ServoCount) return;
    const int angle = m_joints[index]->value();
    // El firmware Info2 usa indices 0..5. El anterior usa 1..6.
    if (protocol() == Protocol::Info2)
        sendLine(QString("SET,%1,%2").arg(index).arg(angle));
    else
        sendLine(QString("J,%1,%2").arg(index+1).arg(angle));
}

bool MainWindow::sendFullPose(const QVector<int> &pose, int milliseconds) {
    if (!m_serial.isOpen() || pose.size() != ServoCount) return false;
    QStringList args;
    for (int angle : pose) args << QString::number(angle);
    if (protocol() == Protocol::Info2) {
        return sendLine(QString("MOVE,%1,%2").arg(milliseconds).arg(args.join(',')));
    }
    const bool ok1 = sendLine(QString("T,%1").arg(milliseconds));
    const bool ok2 = sendLine("A," + args.join(','));
    return ok1 && ok2;
}

void MainWindow::processInput() {
    // Qt puede entregar muchas tramas completas juntas o una sola trama en
    // varios bloques. Antes se borraba TODO el lote si superaba 4096 bytes:
    // esto perdia tambien las lecturas DHT, aun cuando cada linea era valida.
    // Ahora se limita cada LINEA individual; el resto del lote se procesa.
    constexpr int MaxLineBytes = 256; // firmware: respuesta ASCII muy inferior a 256 bytes
    const QByteArray bytes = m_serial.readAll();
    for (const char byte : bytes) {
        if (m_discardingSerialLine) {
            // Descarta solamente la linea incompleta, nunca las siguientes.
            if (byte == '\n') {
                m_discardingSerialLine = false;
                logSerial("INFO", "Fin de trama irregular; recepcion reanudada.");
            }
            continue;
        }
        if (byte == '\n') {
            const QByteArray line = m_inputBuffer.trimmed();
            m_inputBuffer.clear();
            if (!line.isEmpty()) {
                const QString recibido = QString::fromUtf8(line);
                logSerial("RX", recibido);
                processLine(recibido);
            }
        } else if (byte != '\r') {
            if (m_inputBuffer.size() < MaxLineBytes) {
                m_inputBuffer.append(byte);
            } else {
                // No silenciar el problema: registrar HEX para diferenciar
                // errores de baudrate, ruido o firmware con protocolo distinto.
                const QByteArray muestra = m_inputBuffer.left(64).toHex(' ');
                logSerial("ERROR", "Trama RX sin LF de mas de 256 bytes. Muestra HEX: " +
                          QString::fromLatin1(muestra.constData()) +
                          ". Revisar velocidad 115200 y sketch cargado.");
                setStatus("Trama serial sin salto de linea (>256 B). Ctrl+L para ver datos HEX.");
                m_inputBuffer.clear();
                m_discardingSerialLine = true;
            }
        }
    }
}

void MainWindow::setDhtSinDatos(const QString &motivo) {
    if (m_dhtTemperatura) m_dhtTemperatura->setText("TEMPERATURA: --.- °C");
    if (m_dhtHumedad) m_dhtHumedad->setText("HUMEDAD: --.- %");
    if (m_dhtEstado) {
        m_dhtEstado->setText(motivo);
        m_dhtEstado->setStyleSheet("color: #7a4b16;");
    }
}

void MainWindow::processLine(const QString &line) {
    // DHT,<temperatura_C>,<humedad_relativa> | DHT,ERR
    if (line.startsWith("DHT,")) {
        const QStringList campos = line.split(',');
        if (campos.size() == 2 && campos[1] == "ERR") {
            m_dhtTimeout.stop();
            setDhtSinDatos("ERROR DE LECTURA: revisar DHT22");
            if (m_autoState == AutoState::Measuring)
                stopAuto(true, "Medicion cancelada: error del DHT22. No se ejecutaron rutas.");
            return;
        }
        if (campos.size() != 3) {
            setDhtSinDatos("TRAMA DHT22 INVALIDA");
            if (m_autoState == AutoState::Measuring)
                stopAuto(true, "Medicion cancelada: trama DHT22 invalida.");
            return;
        }
        bool tempOk = false, humedadOk = false;
        const double temp = campos[1].toDouble(&tempOk);
        const double hum = campos[2].toDouble(&humedadOk);
        if (!tempOk || !humedadOk || !std::isfinite(temp) || !std::isfinite(hum)
            || temp < -40 || temp > 80 || hum < 0 || hum > 100) {
            setDhtSinDatos("LECTURA DHT22 FUERA DE RANGO");
            if (m_autoState == AutoState::Measuring)
                stopAuto(true, "Medicion cancelada: lectura del sensor invalida.");
            return;
        }
        m_dhtTimeout.start();
        // Solo contar lecturas NUEVAS durante la ventana de medicion.
        if (m_autoState == AutoState::Measuring && m_measureClock.isValid()
            && m_measureClock.elapsed() < 10000)
        {
            m_humiditySamples.append(hum);
            m_temperatureSamples.append(temp);
        }
        m_dhtTemperatura->setText(QString("TEMPERATURA: %1 °C").arg(temp, 0, 'f', 1));
        m_dhtHumedad->setText(QString("HUMEDAD: %1 %").arg(hum, 0, 'f', 1));
        // Indicador instantaneo: la ruta automatica utiliza PROMEDIOS de 10 s.
        const bool hrFuera = hum < m_autoHumidityMin->value() || hum > m_autoHumidityMax->value();
        const bool tempFuera = temp < m_autoTempMin->value() || temp > m_autoTempMax->value();
        if (hrFuera || tempFuera) {
            m_dhtEstado->setText(hrFuera && tempFuera ? "ALERTA: HUMEDAD Y TEMPERATURA FUERA DE RANGO"
                : hrFuera ? "ALERTA: HUMEDAD AMBIENTAL FUERA DEL RANGO"
                          : "ALERTA: TEMPERATURA FUERA DEL RANGO");
            m_dhtEstado->setStyleSheet("color: #b42318;");
        } else {
            m_dhtEstado->setText("CONDICIONES AMBIENTALES DENTRO DE LIMITES (NO CERTIFICA EL OBJETO)");
            m_dhtEstado->setStyleSheet("color: #197447;");
        }
        return;
    }

    if (line.startsWith("ERR")) {
        const QString detalle = "ESP32: " + line + " | ultima orden: " + m_lastCommand +
                                " | Ctrl+L: ver TX/RX";
        if (m_autoRunning) stopAuto(true, "Ciclo abortado por respuesta ESP32: " + line);
        else setStatus(detalle);
        return;
    }
    if (line == "PONG" || line.startsWith("READY")) {
        setStatus("ESP32 responde: " + line); return;
    }
    if (line.startsWith("POS,") || line.startsWith("STATE,")) {
        const QStringList parts = line.split(',');
        if (parts.size() != ServoCount+1) return;
        QVector<int> pose;
        for (int i=1; i<parts.size(); ++i) {
            bool ok = false;
            const int value = parts[i].toInt(&ok);
            if (!ok || value < 0 || value > 180) return;
            pose.append(value);
        }
        if (!m_playing && !m_autoRunning) {
            m_receivingState = true;
            setPose(pose);
            m_receivingState = false;
        }
        return;
    }
    if (line.startsWith("OK,BOTON_STOP")) {
        if (m_autoRunning) stopAuto(false, "Ciclo detenido: boton fisico de parada.");
        stopPlayback(false);
        setStatus("Boton de parada fisico activado en ESP32.");
        return;
    }
    // Las respuestas OK (incluida OK,STOP) se aceptan sin interrumpir la interfaz.
}

void MainWindow::setStatus(const QString &message) {
    if (m_status) m_status->setText(message);
}

void MainWindow::logSerial(const QString &direction, const QString &line) {
    const QString entry = QString("[%1] %2 %3")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), direction, line);
    m_serialHistory.append(entry);
    while (m_serialHistory.size() > 300) m_serialHistory.removeFirst();
}

void MainWindow::showSerialDiagnostic() {
    QDialog dialog(this);
    dialog.setWindowTitle("Diagnostico ESP32: TX / RX");
    dialog.resize(850, 570);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel("Conecta el ESP32, espera 4 segundos y mueve UN eje unos pocos grados. "
                            "TX = enviado por Qt; RX = recibido del ESP32. "
                            "Copia este registro para identificar la trama invalida.");
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *registro = new QTextEdit;
    registro->setReadOnly(true);
    registro->setPlainText(m_serialHistory.join('\n'));
    layout->addWidget(registro);
    auto *refresh = new QPushButton("ACTUALIZAR REGISTRO");
    connect(refresh, &QPushButton::clicked, &dialog, [this, registro] {
        registro->setPlainText(m_serialHistory.join('\n'));
        registro->moveCursor(QTextCursor::End);
    });
    layout->addWidget(refresh);
    dialog.exec();
}

void MainWindow::recordPose() {
    if (m_poses.size() >= 10000) {
        QMessageBox::warning(this, "Limite", "Se permite hasta 10000 poses.");
        return;
    }
    m_poses.append(currentPose());
    updateRecordList();
    setStatus(QString("Guardada postura %1 con los seis angulos.").arg(m_poses.size()));
}

void MainWindow::updateRecordList() {
    m_records->clear();
    for (int i=0; i<m_poses.size(); ++i) {
        QStringList data;
        for (int angle : m_poses[i]) data << QString::number(angle);
        m_records->addItem(QString("%1   [%2]").arg(i+1, 2, 10, QChar('0')).arg(data.join("  ")));
    }
    m_sequenceCount->setText(QString("%1 POSICIONES GUARDADAS").arg(m_poses.size()));
}

void MainWindow::startPlayback() {
    if (m_autoRunning) return;
    if (m_poses.isEmpty()) {
        QMessageBox::information(this, "Sin posiciones", "Primero guarda al menos una posicion.");
        return;
    }
    if (!m_serial.isOpen()) {
        QMessageBox::warning(this, "Sin conexion", "Conecta el ESP32 para reproducir los movimientos.");
        return;
    }
    stopPlayback(false);
    m_playbackIndex = 0;
    m_playing = true;
    m_playbackTimer.start(m_duration->value());
    playNext();
}

void MainWindow::playNext() {
    if (!m_playing) return;
    if (m_playbackIndex >= m_poses.size()) {
        if (!m_repeat->isChecked()) {
            stopPlayback(false);
            setStatus("Secuencia terminada correctamente.");
            return;
        }
        m_playbackIndex = 0;
    }
    const QVector<int> pose = m_poses[m_playbackIndex];
    setPose(pose);
    sendFullPose(pose, m_duration->value());
    m_records->setCurrentRow(m_playbackIndex);
    setStatus(QString("Reproduciendo %1 / %2 - paso %3 ms")
              .arg(m_playbackIndex+1).arg(m_poses.size()).arg(m_duration->value()));
    ++m_playbackIndex;
}

void MainWindow::stopPlayback(bool sendStop) {
    if (m_autoRunning) {
        stopAuto(sendStop, "Ciclo detenido por el usuario.");
        sendStop = false;  // STOP ya fue enviado por stopAuto; no duplicarlo.
    }
    m_playbackTimer.stop();
    m_playing = false;
    for (auto &timer : m_jointDebounce) timer.stop();
    if (sendStop && m_serial.isOpen()) {
        sendLine("STOP"); // Congela la consigna, pero NO elimina el torque.
        sendLine("GET");
        setStatus("Movimiento detenido. Los servos siguen energizados (torque activo).");
    } else if (sendStop) {
        setStatus("Reproduccion detenida; ESP32 desconectado.");
    }
}

void MainWindow::homePose() {
    if (m_autoRunning) return;
    stopPlayback(false);
    QVector<int> home(ServoCount, 90);
    setPose(home);
    sendFullPose(home, m_duration->value());
    setStatus("Regreso a 90 grados ordenado. Se conserva la secuencia grabada.");
}

void MainWindow::clearSequence() {
    if (!m_poses.isEmpty() &&
        QMessageBox::question(this, "Borrar secuencia", "Eliminar todas las posiciones grabadas?")
         != QMessageBox::Yes) return;
    stopPlayback(false);
    m_poses.clear();
    updateRecordList();
    setStatus("Secuencia borrada. La postura actual no se modifico.");
}

void MainWindow::exportSequence() {
    if (m_poses.isEmpty()) {
        QMessageBox::information(this, "Sin datos", "No hay movimientos para exportar.");
        return;
    }
    const QString fileName = QFileDialog::getSaveFileName(this, "Exportar posiciones",
                        "movimientos_brazo.json", "Archivos JSON (*.json)");
    if (fileName.isEmpty()) return;
    QJsonObject root;
    root.insert("format", "fabri-creator-esp32-6servo"); // Compatible con la interfaz anterior
    root.insert("version", 1);
    root.insert("step_duration_ms", m_duration->value());
    QJsonArray joints;
    for (const QString &name : ServoNames) joints.append(name);
    root.insert("joints", joints);
    QJsonArray poses;
    for (const auto &pose : m_poses) {
        QJsonArray angles;
        for (int angle : pose) angles.append(angle);
        poses.append(angles);
    }
    root.insert("positions", poses);
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        QMessageBox::critical(this, "Error", "No se pudo escribir el archivo: " + file.errorString());
        return;
    }
    setStatus(QString("Exportadas %1 posiciones a JSON.").arg(m_poses.size()));
}

void MainWindow::importSequence() {
    const QString fileName = QFileDialog::getOpenFileName(this, "Importar posiciones",
                        QString(), "Archivos JSON (*.json)");
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "No se pudo abrir: " + file.errorString());
        return;
    }
    if (file.size() > 1024*1024) {
        QMessageBox::warning(this, "Error", "JSON demasiado grande (maximo 1 MiB).");
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, "Formato incorrecto", "Archivo JSON invalido.");
        return;
    }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != "fabri-creator-esp32-6servo" ||
        !root.value("positions").isArray() || !root.value("step_duration_ms").isDouble()) {
        QMessageBox::warning(this, "Formato incorrecto", "El JSON no es una secuencia FABRI CREATOR valida.");
        return;
    }
    const QJsonArray poses = root.value("positions").toArray();
    if (poses.size() > 10000) {
        QMessageBox::warning(this, "Limite", "Maximo 10000 posiciones.");
        return;
    }
    const double time = root.value("step_duration_ms").toDouble();
    if (time != int(time) || time < 100 || time > 10000) {
        QMessageBox::warning(this, "Tiempo invalido", "El tiempo debe estar entre 100 y 10000 ms.");
        return;
    }
    QVector<QVector<int>> loaded;
    for (const QJsonValue &entry : poses) {
        if (!entry.isArray() || entry.toArray().size() != ServoCount) {
            QMessageBox::warning(this, "Datos invalidos", "Cada postura debe tener seis valores.");
            return;
        }
        QVector<int> pose;
        const QJsonArray angles = entry.toArray();
        for (int i=0; i<ServoCount; ++i) {
            if (!angles[i].isDouble()) {
                QMessageBox::warning(this, "Datos invalidos", "Los angulos deben ser enteros.");
                return;
            }
            const double number = angles[i].toDouble();
            if (number != int(number) || number < m_joints[i]->minimum() || number > m_joints[i]->maximum()) {
                QMessageBox::warning(this, "Fuera de limites",
                    QString("El eje %1 tiene un angulo que excede los limites configurados.").arg(i+1));
                return;
            }
            pose.append(int(number));
        }
        loaded.append(pose);
    }
    stopPlayback(false);
    m_poses = loaded;
    m_duration->setValue(int(time));
    updateRecordList();
    setStatus(QString("Importadas %1 posiciones sin cambiar la postura actual.").arg(m_poses.size()));
}

void MainWindow::openLimitsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Calibrar limites de recorrido - NO fuerza del servo");
    QVBoxLayout layout(&dialog);
    auto *warning = new QLabel("Los limites restringen los comandos desde Qt.\n"
                   "No detectan contacto, atascos ni sobrecorriente.\n"
                   "Revisar especialmente el MG90S de la pinza.\n"
                   "El recorrido real depende de PULSO_MIN_US/PULSO_MAX_US "
                   "en BrazoArduino.ino: cambiar los limites de Qt NO cambia el PWM.");
    warning->setWordWrap(true);
    layout.addWidget(warning);
    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel("ARTICULACION"), 0, 0);
    grid->addWidget(new QLabel("MINIMO"), 0, 1);
    grid->addWidget(new QLabel("MAXIMO"), 0, 2);
    QSpinBox *minEditors[ServoCount];
    QSpinBox *maxEditors[ServoCount];
    for (int i=0; i<ServoCount; ++i) {
        grid->addWidget(new QLabel(QString("%1 (%2)").arg(ServoNames[i]).arg(ServoModels[i])), i+1, 0);
        minEditors[i] = new QSpinBox;
        maxEditors[i] = new QSpinBox;
        minEditors[i]->setRange(0, 179);
        maxEditors[i]->setRange(1, 180);
        minEditors[i]->setValue(m_joints[i]->minimum());
        maxEditors[i]->setValue(m_joints[i]->maximum());
        grid->addWidget(minEditors[i], i+1, 1);
        grid->addWidget(maxEditors[i], i+1, 2);
    }
    layout.addLayout(grid);
    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton("CANCELAR");
    auto *accept = new QPushButton("APLICAR LIMITES");
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    layout.addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(accept, &QPushButton::clicked, &dialog, [&, this] {
        for (int i=0; i<ServoCount; ++i) {
            if (minEditors[i]->value() >= maxEditors[i]->value()) {
                QMessageBox::warning(&dialog, "Rango invalido", "El minimo debe ser menor que el maximo.");
                return;
            }
        }
        for (int i=0; i<ServoCount; ++i)
            m_joints[i]->setLimits(minEditors[i]->value(), maxEditors[i]->value());
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted)
        setStatus("Limites de Qt actualizados (no cambian el firmware ni sustituyen proteccion fisica).");
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_autoRunning) stopAuto(false, "Ciclo cancelado al cerrar la aplicacion.");
    // Si se cierra con una secuencia activa, intentar congelar el comando.
    if (m_serial.isOpen()) {
        sendLine("STOP");
        m_serial.flush();
    }
    disconnectSerial();
    QMainWindow::closeEvent(event);
}
// -----------------------------------------------------------------------------
// Ciclo programable de 4 etapas. La medicion de 10 segundos utiliza las
// lecturas DHT22 reales recibidas durante ese intervalo; NO reutiliza la
// ultima lectura anterior y NO verifica humedad dentro del medicamento.
// -----------------------------------------------------------------------------
void MainWindow::buildAutoUi() {
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    pageLayout->addWidget(scroll);

    auto *content = new QWidget;
    content->setStyleSheet(R"(
       QWidget#autoContent { background: #edf2f7; }
       QGroupBox { border: 1px solid #ced9e6; background: white;
                   border-radius: 6px; margin-top: 12px; padding-top: 11px;
                   font-size: 17px; font-weight: bold; color: #152339; }
       QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; }
       QLabel#autoDescription { color: #31425a; font-size: 14px; font-weight: normal; }
       QLabel#autoNotice { color: #8a341d; font-size: 15px; font-weight: bold;
                           background: #fff7ed; padding: 9px; border: 1px solid #fed7aa; }
       QLabel#autoResult { color: #142c45; font-size: 17px; font-weight: bold;
                           background: #dcebf6; padding: 12px; border-radius: 5px; }
       QLabel#autoSamples { color: #23445e; font-size: 15px; background: transparent; }
       QPushButton#autoDark { background: #111827; color: white; padding: 9px;
                             font-weight: bold; border-radius: 4px; }
       QPushButton#autoDark:hover { background: #234360; }
       QPushButton#autoStop { background: #801c27; color: white; padding: 10px;
                             font-weight: bold; border-radius: 4px; }
       QPushButton#autoGo { background: #15688b; color: white; padding: 11px;
                           font-weight: bold; border-radius: 4px; }
       QListWidget { color: #17283e; background: #f6f9fc; border: 1px solid #bdc9d7;
                     font-family: Consolas; font-size: 14px; }
       QSpinBox, QDoubleSpinBox { background: #111827; color: white; min-height: 30px; }
    )");
    content->setObjectName("autoContent");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 22, 26, 26);
    layout->setSpacing(14);

    auto *heading = new QLabel("CLASIFICACION AUTOMATICA DE OBJETOS - 4 ETAPAS");
    heading->setStyleSheet("font: bold 26px 'Segoe UI'; color: #142237;");
    layout->addWidget(heading);
    // Este identificador visible permite saber si Qt esta ejecutando el binario viejo.
    auto *versionInfo = new QLabel(
        "VERSION RANGOS HR+T v3 (17/11/2026) | HUMEDAD 40-70 % Y TEMPERATURA 15-30 °C "
        "| AMBAS OBLIGATORIAS");
    versionInfo->setObjectName("autoVersion");
    versionInfo->setWordWrap(true);
    versionInfo->setStyleSheet(
        "background: #e7f4ec; border: 1px solid #4b986b; color: #195c38; "
        "border-radius: 5px; padding: 9px; font-size: 15px; font-weight: bold;");
    layout->addWidget(versionInfo);
    auto *notice = new QLabel(
        "El DHT22 mide HUMEDAD RELATIVA DEL AIRE junto a la garra, NO humedad interna "
        "ni estado sanitario del medicamento. Los limites configurables de HR y "
        "temperatura solo demuestran una clasificacion ambiental. "
        "No forzar la pinza ni mantenerla bloqueada.");
    notice->setObjectName("autoNotice");
    notice->setWordWrap(true);
    layout->addWidget(notice);

    auto *top = new QHBoxLayout;
    m_autoStart = new QPushButton("INICIAR CICLO COMPLETO");
    m_autoStart->setObjectName("autoGo");
    m_autoStart->setMinimumHeight(47);
    top->addWidget(m_autoStart, 2);
    connect(m_autoStart, &QPushButton::clicked, this, &MainWindow::startAutoCycle);
    m_autoStop = new QPushButton("DETENER CICLO (Ctrl+Espacio)");
    m_autoStop->setObjectName("autoStop");
    m_autoStop->setMinimumHeight(47);
    m_autoStop->setEnabled(false);
    top->addWidget(m_autoStop, 2);
    connect(m_autoStop, &QPushButton::clicked, this, [this] {
        stopAuto(true, "Ciclo detenido manualmente. Los servos conservan torque.");
    });
    layout->addLayout(top);

    // Ambos rangos son obligatorios. Dentro de limites es la interseccion:
    // HR minima <= HR promedio <= HR maxima Y T minima <= T promedio <= T maxima.
    auto *humidityOptions = new QHBoxLayout;
    auto *humidityTitle = new QLabel("HUMEDAD AMBIENTAL:");
    humidityTitle->setStyleSheet("font-weight: bold;");
    humidityOptions->addWidget(humidityTitle);
    humidityOptions->addWidget(new QLabel("Minima:"));
    m_autoHumidityMin = new QSpinBox;
    m_autoHumidityMin->setRange(0, 100);
    m_autoHumidityMin->setValue(40);
    m_autoHumidityMin->setSuffix(" %");
    m_autoHumidityMin->setToolTip("Humedad relativa minima permitida. Limite incluido.");
    m_autoHumidityMin->setMinimumWidth(100);
    humidityOptions->addWidget(m_autoHumidityMin);
    humidityOptions->addWidget(new QLabel("Maxima:"));
    m_autoHumidityMax = new QSpinBox;
    m_autoHumidityMax->setRange(0, 100);
    m_autoHumidityMax->setValue(70);
    m_autoHumidityMax->setSuffix(" %");
    m_autoHumidityMax->setToolTip("Humedad relativa maxima permitida. Limite incluido.");
    m_autoHumidityMax->setMinimumWidth(100);
    humidityOptions->addWidget(m_autoHumidityMax);
    humidityOptions->addStretch(1);
    layout->addLayout(humidityOptions);

    auto *temperatureOptions = new QHBoxLayout;
    auto *temperatureTitle = new QLabel("TEMPERATURA AMBIENTAL:");
    temperatureTitle->setStyleSheet("font-weight: bold;");
    temperatureOptions->addWidget(temperatureTitle);
    temperatureOptions->addWidget(new QLabel("Minima:"));
    m_autoTempMin = new QDoubleSpinBox;
    m_autoTempMin->setRange(-40.0, 80.0); // DHT22.
    m_autoTempMin->setDecimals(1);
    m_autoTempMin->setSingleStep(0.5);
    m_autoTempMin->setSuffix(" °C");
    m_autoTempMin->setValue(15.0);
    m_autoTempMin->setMinimumWidth(105);
    temperatureOptions->addWidget(m_autoTempMin);
    temperatureOptions->addWidget(new QLabel("Maxima:"));
    m_autoTempMax = new QDoubleSpinBox;
    m_autoTempMax->setRange(-40.0, 80.0);
    m_autoTempMax->setDecimals(1);
    m_autoTempMax->setSingleStep(0.5);
    m_autoTempMax->setSuffix(" °C");
    m_autoTempMax->setValue(30.0);
    m_autoTempMax->setMinimumWidth(105);
    temperatureOptions->addWidget(m_autoTempMax);
    temperatureOptions->addStretch(1);
    layout->addLayout(temperatureOptions);

    auto *classificationInfo = new QLabel(
        "RUTA 3: humedad O temperatura por debajo del minimo o por encima del maximo. "
        "RUTA 4: humedad Y temperatura dentro de sus respectivos rangos (extremos incluidos). "
        "Se clasifican los promedios medidos durante 10 segundos; ambos controles estan siempre activos.");
    classificationInfo->setObjectName("autoDescription");
    classificationInfo->setWordWrap(true);
    layout->addWidget(classificationInfo);
    connect(m_autoHumidityMin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int minimum) {
        if (minimum > m_autoHumidityMax->value()) m_autoHumidityMax->setValue(minimum);
        updateAutoRouteDescriptions();
    });
    connect(m_autoHumidityMax, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int maximum) {
        if (maximum < m_autoHumidityMin->value()) m_autoHumidityMin->setValue(maximum);
        updateAutoRouteDescriptions();
    });
    connect(m_autoTempMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double minimum) {
        if (minimum > m_autoTempMax->value()) m_autoTempMax->setValue(minimum);
        updateAutoRouteDescriptions();
    });
    connect(m_autoTempMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double maximum) {
        if (maximum < m_autoTempMin->value()) m_autoTempMin->setValue(maximum);
        updateAutoRouteDescriptions();
    });

    // Configuracion de repeticiones independiente de las operaciones de archivo.
    auto *cycleOptions = new QHBoxLayout;
    m_autoRepeatButton = new QPushButton("DEFINIR BUCLES");
    m_autoRepeatButton->setObjectName("autoDark");
    m_autoRepeatButton->setMinimumHeight(43);
    m_autoRepeatButton->setToolTip("Elegir entre 1 y 1000 ejecuciones completas; 1 significa una sola vez.");
    cycleOptions->addWidget(m_autoRepeatButton, 1);
    connect(m_autoRepeatButton, &QPushButton::clicked, this, &MainWindow::configureAutoRepeats);
    m_autoRepeatLabel = new QLabel("BUCLES CONFIGURADOS: 1");
    m_autoRepeatLabel->setStyleSheet("color: #142c45; font-size: 15px; font-weight: bold;");
    cycleOptions->addWidget(m_autoRepeatLabel, 2);
    layout->addLayout(cycleOptions);

    // Las dos acciones quedan juntas y visibles sin desplazarse al pie de la pagina.
    // IMPORTAR usa el mismo formato JSON que EXPORTAR y recupera toda la configuracion.
    auto *cycleFiles = new QHBoxLayout;
    m_autoExport = new QPushButton("EXPORTAR CICLO COMPLETO (JSON)");
    m_autoExport->setObjectName("autoGo");
    m_autoExport->setMinimumHeight(43);
    m_autoExport->setToolTip("Guarda cuatro etapas, duracion, ambos rangos ambientales y bucles en JSON.");
    cycleFiles->addWidget(m_autoExport, 1);
    connect(m_autoExport, &QPushButton::clicked, this, &MainWindow::exportAutoCycle);

    m_autoLoad = new QPushButton("IMPORTAR CICLO COMPLETO (JSON)");
    m_autoLoad->setObjectName("autoDark");
    m_autoLoad->setMinimumHeight(43);
    m_autoLoad->setToolTip("Importa cuatro etapas, duracion, rangos ambientales y bucles. No mueve los servos.");
    cycleFiles->addWidget(m_autoLoad, 1);
    connect(m_autoLoad, &QPushButton::clicked, this, &MainWindow::loadAutoProfile);
    layout->addLayout(cycleFiles);

    m_autoResult = new QLabel("RANGOS HR+T v3 LISTO: se controlan SIEMPRE humedad y temperatura. "
                              "Graba los movimientos en las etapas 1, 3 y 4.");
    m_autoResult->setObjectName("autoResult");
    m_autoResult->setWordWrap(true);
    m_autoResult->setMinimumHeight(55);
    layout->addWidget(m_autoResult);
    m_autoSamplesLabel = new QLabel("Durante 10 segundos se promedian al menos 2 pares nuevos de humedad y temperatura del DHT22.");
    m_autoSamplesLabel->setObjectName("autoSamples");
    m_autoSamplesLabel->setWordWrap(true);
    layout->addWidget(m_autoSamplesLabel);

    constexpr const char *titles[4] = {
      "1 - AGARRAR OBJETO",
      "2 - MEDIR AMBIENTE (HR Y T, 10 SEGUNDOS)",
      "3 - RUTA: FUERA DE LOS LIMITES AMBIENTALES",
      "4 - RUTA: DENTRO DE LOS LIMITES AMBIENTALES"
    };
    constexpr const char *descriptions[4] = {
      "Guardar pasos: abrir pinza, acercarse y cerrar SUAVEMENTE. El ultimo paso debe quedar agarrando.",
      "Opcional: guardar posiciones para acercar el sensor al objeto. Despues la garra permanece quieta 10 s.",
      "Se ejecuta cuando la HR O la temperatura esta por debajo o por encima de su rango.",
      "Se ejecuta solo cuando AMBOS promedios estan dentro de sus respectivos rangos."
    };
    auto *cards = new QGridLayout;
    cards->setSpacing(16);
    for (int i = 0; i < AutoStages; ++i) {
        m_autoCards[i] = new QGroupBox(QString::fromUtf8(titles[i]));
        auto *cardLayout = new QVBoxLayout(m_autoCards[i]);
        cardLayout->setSpacing(9);
        auto *desc = new QLabel(QString::fromUtf8(descriptions[i]));
        desc->setObjectName("autoDescription");
        desc->setWordWrap(true);
        cardLayout->addWidget(desc);
        if (i == 2 || i == 3) m_autoRouteDescriptions[i - 2] = desc;
        m_autoLists[i] = new QListWidget;
        m_autoLists[i]->setMinimumHeight(119);
        m_autoLists[i]->setSelectionMode(QAbstractItemView::SingleSelection);
        m_autoLists[i]->setToolTip("Lista de posturas con los seis servos. Cada una dura el tiempo por paso del panel manual.");
        cardLayout->addWidget(m_autoLists[i]);
        auto *line = new QHBoxLayout;
        auto *add = new QPushButton("+ GUARDAR POSE ACTUAL");
        add->setObjectName("autoDark");
        line->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this,i] { addAutoPose(i); });
        auto *del = new QPushButton("QUITAR SELECCION");
        del->setObjectName("autoDark");
        line->addWidget(del);
        connect(del, &QPushButton::clicked, this, [this,i] { deleteAutoPose(i); });
        cardLayout->addLayout(line);
        auto *actions = new QHBoxLayout;
        auto *test = new QPushButton("PROBAR MOVIMIENTO");
        test->setObjectName("autoDark");
        actions->addWidget(test);
        connect(test, &QPushButton::clicked, this, [this,i] { testAutoStage(i); });
        auto *clear = new QPushButton("VACIAR ETAPA");
        clear->setObjectName("autoDark");
        actions->addWidget(clear);
        connect(clear, &QPushButton::clicked, this, [this,i] { clearAutoStage(i); });
        cardLayout->addLayout(actions);
        if (i == 1) {
            m_autoMeasureOnlyButton = new QPushButton("MEDIR SOLO 10 SEGUNDOS");
            m_autoMeasureOnlyButton->setObjectName("autoGo");
            cardLayout->addWidget(m_autoMeasureOnlyButton);
            connect(m_autoMeasureOnlyButton, &QPushButton::clicked,
                    this, &MainWindow::startMeasurementOnly);
        }
        cards->addWidget(m_autoCards[i], i / 2, i % 2);
    }
    layout->addLayout(cards);
    updateAutoRouteDescriptions();

    // Se conserva el guardado anterior como acceso adicional; el boton de
    // importacion completa ahora se encuentra junto al de exportacion arriba.
    m_autoSave = new QPushButton("GUARDAR LAS 4 ETAPAS EN JSON");
    m_autoSave->setObjectName("autoDark");
    m_autoSave->setMinimumHeight(38);
    m_autoSave->setToolTip("Guarda un perfil compatible con IMPORTAR CICLO COMPLETO (JSON).");
    connect(m_autoSave, &QPushButton::clicked, this, &MainWindow::saveAutoProfile);
    layout->addWidget(m_autoSave);
    auto *foot = new QLabel(
      "Uso: en CONTROL MANUAL mover los 6 ejes, volver aqui y pulsar GUARDAR POSE ACTUAL. "
      "Repetir para cada paso. Para el ciclo, configurar movimientos de las etapas 1, 3 y 4 "
      "(la etapa 2 puede no tener movimientos). Guardar JSON para conservarlos al cerrar Qt. "
      "El tiempo por paso se configura en CONTROL MANUAL. "
      "Cada BUCLE vuelve a agarrar, mide 10 s y ejecuta la ruta seleccionada. "
      "Antes de repetir, la ruta debe haber soltado el objeto y dejado al brazo listo "
      "para tomar otra pieza; no se verifica el agarre fisico.");
    foot->setObjectName("autoDescription");
    foot->setWordWrap(true);
    layout->addWidget(foot);
    layout->addStretch(1);
    scroll->setWidget(content);
    m_tabs->addTab(page, "CICLO AUTOMATICO / DHT22");
    updateAutoLists();
}

// Reglas visibles en cada tarjeta; los limites estan incluidos en la ruta 4.
void MainWindow::updateAutoRouteDescriptions() {
    if (!m_autoRouteDescriptions[0] || !m_autoRouteDescriptions[1] ||
        !m_autoHumidityMin || !m_autoHumidityMax || !m_autoTempMin || !m_autoTempMax) return;
    const int hrMin = m_autoHumidityMin->value();
    const int hrMax = m_autoHumidityMax->value();
    const QString tMin = QString::number(m_autoTempMin->value(), 'f', 1);
    const QString tMax = QString::number(m_autoTempMax->value(), 'f', 1);
    m_autoRouteDescriptions[0]->setText(QString(
        "FUERA DE LIMITES si AL MENOS UNA condicion ocurre: "
        "HR promedio < %1 % O HR promedio > %2 % O temperatura promedio < %3 °C "
        "O temperatura promedio > %4 °C."
        ).arg(hrMin).arg(hrMax).arg(tMin).arg(tMax));
    m_autoRouteDescriptions[1]->setText(QString(
        "DENTRO DE LIMITES solo si AMBAS condiciones se cumplen: "
        "HR promedio entre %1 % y %2 % Y temperatura promedio entre %3 °C "
        "y %4 °C (todos los limites incluidos)."
        ).arg(hrMin).arg(hrMax).arg(tMin).arg(tMax));
}

void MainWindow::updateAutoLists() {
    for (int stage = 0; stage < AutoStages; ++stage) {
        if (!m_autoLists[stage]) continue;
        m_autoLists[stage]->clear();
        for (int j = 0; j < m_autoSequences[stage].size(); ++j) {
            QStringList angles;
            for (int a : m_autoSequences[stage][j]) angles << QString::number(a);
            m_autoLists[stage]->addItem(QString("%1)  [%2]").arg(j + 1).arg(angles.join(" , ")));
        }
        m_autoCards[stage]->setToolTip(QString("%1 posturas grabadas.").arg(m_autoSequences[stage].size()));
    }
}

void MainWindow::addAutoPose(int stage) {
    if (m_autoRunning || stage < 0 || stage >= AutoStages) return;
    if (m_autoSequences[stage].size() >= 100) {
        QMessageBox::warning(this, "Limite", "Maximo 100 poses por etapa.");
        return;
    }
    m_autoSequences[stage].append(currentPose());
    updateAutoLists();
    m_autoLists[stage]->setCurrentRow(m_autoSequences[stage].size() - 1);
    m_autoResult->setText(QString("Etapa %1: guardada postura %2. Aun no se guarda en disco: usar GUARDAR JSON.")
                          .arg(stage + 1).arg(m_autoSequences[stage].size()));
}

void MainWindow::deleteAutoPose(int stage) {
    if (m_autoRunning || stage < 0 || stage >= AutoStages) return;
    const int index = m_autoLists[stage]->currentRow();
    if (index < 0 || index >= m_autoSequences[stage].size()) return;
    m_autoSequences[stage].removeAt(index);
    updateAutoLists();
    m_autoResult->setText(QString("Eliminada postura de la etapa %1.").arg(stage+1));
}

void MainWindow::clearAutoStage(int stage) {
    if (m_autoRunning || stage < 0 || stage >= AutoStages || m_autoSequences[stage].isEmpty()) return;
    if (QMessageBox::question(this, "Vaciar etapa", QString("Borrar las %1 posturas de la etapa %2?")
                             .arg(m_autoSequences[stage].size()).arg(stage+1)) != QMessageBox::Yes) return;
    m_autoSequences[stage].clear();
    updateAutoLists();
    m_autoResult->setText(QString("Etapa %1 vaciada.").arg(stage+1));
}

bool MainWindow::validateAutoPoses(int stage) const {
    for (const QVector<int> &pose : m_autoSequences[stage]) {
        if (pose.size() != ServoCount) return false;
        for (int i = 0; i < ServoCount; ++i)
            if (pose[i] < m_joints[i]->minimum() || pose[i] > m_joints[i]->maximum()) return false;
    }
    return true;
}

void MainWindow::setAutoBusy(bool busy) {
    m_autoStart->setEnabled(!busy);
    m_autoStop->setEnabled(busy);
    m_autoLoad->setEnabled(!busy);
    m_autoSave->setEnabled(!busy);
    m_autoHumidityMin->setEnabled(!busy);
    m_autoHumidityMax->setEnabled(!busy);
    m_autoTempMin->setEnabled(!busy);
    m_autoTempMax->setEnabled(!busy);
    m_autoRepeatButton->setEnabled(!busy);
    m_autoExport->setEnabled(!busy);
    for (QGroupBox *group : m_autoCards) group->setEnabled(!busy);
    m_view->setEnabled(!busy); // evitar comandos manuales durante los movimientos automaticos
}

void MainWindow::startAutoCycle() {
    if (m_autoRunning) return;
    if (!m_serial.isOpen() || protocol() != Protocol::Info2) {
        QMessageBox::warning(this, "Conexion requerida",
                             "Conectar el ESP32 en modo Info2: SET / MOVE antes de iniciar.");
        return;
    }
    if (m_autoSequences[0].isEmpty() || m_autoSequences[2].isEmpty() || m_autoSequences[3].isEmpty()) {
        QMessageBox::warning(this, "Faltan movimientos",
                             "Grabar al menos una postura en AGARRAR, FUERA DE LIMITES y DENTRO DE LIMITES. "
                             "La etapa de MEDIR es opcional.");
        return;
    }
    for (int i=0; i<AutoStages; ++i) {
        if (!validateAutoPoses(i)) {
            QMessageBox::warning(this, "Posturas fuera de limites",
                QString("La etapa %1 contiene angulos fuera de los limites del control manual. ")
                .arg(i+1) + "Corregi el perfil o los limites antes de iniciar.");
            return;
        }
    }
    if (m_autoRepeatCount > 1) {
        const auto confirm = QMessageBox::question(this, "Confirmar ciclos repetidos",
            QString("Se ejecutaran %1 bucles completos. Cada vuelta mueve la pinza, "
                    "realiza una medicion NUEVA de 10 s y clasifica nuevamente.\n\n"
                    "Para continuar, las rutas 3 y 4 deben SOLTAR la pieza y dejar "
                    "al brazo listo para la siguiente. No hay deteccion de agarre ni "
                    "proteccion contra bloqueo del servo.\n\nContinuar?").arg(m_autoRepeatCount),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes) return;
    }
    stopPlayback(false);
    m_autoRunning = true;
    m_autoTestMode = false;
    m_autoMeasurementOnly = false;
    m_autoRepeatTarget = m_autoRepeatCount;
    m_autoCycleNumber = 1;
    m_tabs->setCurrentIndex(1);
    setAutoBusy(true);
    m_autoSamplesLabel->setText(QString("BUCLE 1/%1: tomando objeto. Luego se medira durante 10 segundos.")
                                .arg(m_autoRepeatTarget));
    startAutoStage(0);
}

void MainWindow::testAutoStage(int stage) {
    if (m_autoRunning || stage < 0 || stage >= AutoStages) return;
    if (!m_serial.isOpen() || protocol() != Protocol::Info2) {
        QMessageBox::warning(this, "Conexion requerida", "Conectar ESP32 con protocolo Info2 para probar un movimiento.");
        return;
    }
    if (m_autoSequences[stage].isEmpty()) {
        QMessageBox::information(this, "Sin pasos", "Graba al menos una postura para probar esta etapa.");
        return;
    }
    if (!validateAutoPoses(stage)) {
        QMessageBox::warning(this, "Angulos fuera de limites", "Hay posturas fuera del rango permitido.");
        return;
    }
    stopPlayback(false);
    m_autoRunning = true;
    m_autoTestMode = true;
    m_autoMeasurementOnly = false;
    setAutoBusy(true);
    m_autoSamplesLabel->setText("Prueba manual de etapa: no se ejecutaran rutas adicionales.");
    startAutoStage(stage);
}

void MainWindow::startMeasurementOnly() {
    if (m_autoRunning) return;
    if (!m_serial.isOpen() || protocol() != Protocol::Info2) {
        QMessageBox::warning(this, "Conexion requerida", "Conectar ESP32 con protocolo Info2.");
        return;
    }
    stopPlayback(false);
    m_autoRunning = true;
    m_autoTestMode = false;
    m_autoMeasurementOnly = true;
    setAutoBusy(true);
    beginAutoMeasurement();
}

void MainWindow::startAutoStage(int stage) {
    if (!m_autoRunning) return;
    if (stage < 0 || stage >= AutoStages) {
        stopAuto(true, "Error: etapa no valida.");
        return;
    }
    m_autoActiveStage = stage;
    m_autoNextIndex = 0;
    if (m_autoTestMode) m_autoState = AutoState::Testing;
    else if (stage == 0) m_autoState = AutoState::Picking;
    else if (stage == 1) m_autoState = AutoState::Positioning;
    else if (stage == 2) m_autoState = AutoState::WetRoute;
    else m_autoState = AutoState::DryRoute;
    const QString prefix = m_autoTestMode ? "PRUEBA" :
        QString("BUCLE %1/%2").arg(m_autoCycleNumber).arg(m_autoRepeatTarget);
    m_autoResult->setText(QString("%1 - EJECUTANDO ETAPA %2 - PASOS: %3")
                          .arg(prefix).arg(stage+1).arg(m_autoSequences[stage].size()));
    sendAutoStep();
}

void MainWindow::sendAutoStep() {
    if (!m_autoRunning || m_autoActiveStage < 0 || m_autoActiveStage >= AutoStages) return;
    const QVector<QVector<int>> &steps = m_autoSequences[m_autoActiveStage];
    if (m_autoNextIndex >= steps.size()) {
        completeAutoStage();
        return;
    }
    const QVector<int> pose = steps[m_autoNextIndex];
    ++m_autoNextIndex;
    setPose(pose); // solamente actualiza la representacion local; tambien enviamos MOVE
    if (!sendFullPose(pose, m_duration->value())) {
        stopAuto(true, "Error de escritura serie. Ciclo detenido.");
        return;
    }
    const QString prefix = m_autoTestMode ? "PRUEBA" :
        QString("BUCLE %1/%2").arg(m_autoCycleNumber).arg(m_autoRepeatTarget);
    m_autoResult->setText(QString("%1 - ETAPA %2: movimiento %3 de %4; %5 ms por paso.")
                          .arg(prefix).arg(m_autoActiveStage+1).arg(m_autoNextIndex)
                          .arg(steps.size()).arg(m_duration->value()));
    // El sketch interpola durante el tiempo solicitado; esperar a que termine.
    m_autoStepTimer.start(m_duration->value() + 150);
}

void MainWindow::completeAutoStage() {
    if (!m_autoRunning) return;
    if (m_autoTestMode) {
        stopAuto(false, QString("Prueba de etapa %1 finalizada. ")
                 .arg(m_autoActiveStage+1) + "El comando no confirma agarre fisico.");
        return;
    }
    if (m_autoActiveStage == 0) {
        startAutoStage(1); // puede estar vacia: mide donde quedo la garra
    } else if (m_autoActiveStage == 1) {
        beginAutoMeasurement();
    } else {
        const QString route = (m_autoActiveStage == 2)
            ? "FUERA DE LOS LIMITES AMBIENTALES" : "DENTRO DE LOS LIMITES AMBIENTALES";
        if (m_autoCycleNumber < m_autoRepeatTarget) {
            const int finished = m_autoCycleNumber;
            ++m_autoCycleNumber;
            m_autoResult->setText(QString("BUCLE %1/%2 completado: ruta %3. "
                "Preparando BUCLE %4/%2...")
                .arg(finished).arg(m_autoRepeatTarget).arg(route).arg(m_autoCycleNumber));
            // Pausa cancelable entre bucles. No se solapa con el temporizador de pasos.
            m_autoNextCycleTimer.start(800);
        } else {
            stopAuto(false, QString("CICLO COMPLETADO: %1/%2 bucles ejecutados. "
                "Ultima ruta: %3. Los servos conservan torque; no se certifica el objeto.")
                .arg(m_autoCycleNumber).arg(m_autoRepeatTarget).arg(route));
        }
    }
}

void MainWindow::beginAutoMeasurement() {
    if (!m_autoRunning) return;
    m_autoState = AutoState::Measuring;
    m_autoActiveStage = 1;
    m_humiditySamples.clear();
    m_temperatureSamples.clear();
    m_measureClock.start();
    const QString prefix = m_autoMeasurementOnly ? "SOLO MEDICION" :
        QString("BUCLE %1/%2").arg(m_autoCycleNumber).arg(m_autoRepeatTarget);
    m_autoResult->setText(prefix + ": MEDICION ACTIVA durante 10 segundos en la ultima postura ordenada.");
    m_autoSamplesLabel->setText(prefix + ": quedan 10 segundos - 0 muestras nuevas recibidas.");
    m_autoMeasureTimer.start();
}

void MainWindow::updateAutoMeasurement() {
    if (!m_autoRunning || m_autoState != AutoState::Measuring) return;
    const qint64 elapsed = m_measureClock.elapsed();
    const int secondsLeft = static_cast<int>(qMax<qint64>(0, (10000 - elapsed + 999) / 1000));
    const QString prefix = m_autoMeasurementOnly ? "SOLO MEDICION" :
        QString("BUCLE %1/%2").arg(m_autoCycleNumber).arg(m_autoRepeatTarget);
    const QString limits = QString("HR: %1 a %2 % | T: %3 a %4 °C")
        .arg(m_autoHumidityMin->value()).arg(m_autoHumidityMax->value())
        .arg(m_autoTempMin->value(), 0, 'f', 1).arg(m_autoTempMax->value(), 0, 'f', 1);
    m_autoSamplesLabel->setText(QString("%1 | Quedan %2 s | Pares DHT22: %3 | %4")
        .arg(prefix).arg(secondsLeft).arg(m_humiditySamples.size()).arg(limits));
    if (elapsed < 10000) return;
    m_autoMeasureTimer.stop();
    if (m_humiditySamples.size() < 2 || m_humiditySamples.size() != m_temperatureSamples.size()) {
        stopAuto(true, "SIN CLASIFICACION: menos de 2 pares de lecturas validos en 10 segundos. "
                "No se activa ninguna ruta.");
        return;
    }
    double sumHumidity = 0.0, sumTemperature = 0.0;
    for (double value : m_humiditySamples) sumHumidity += value;
    for (double value : m_temperatureSamples) sumTemperature += value;
    const double avgHumidity = sumHumidity / m_humiditySamples.size();
    const double avgTemperature = sumTemperature / m_temperatureSamples.size();
    const int chosen = ambient::selectStage(avgHumidity, avgTemperature,
        m_autoHumidityMin->value(), m_autoHumidityMax->value(),
        m_autoTempMin->value(), m_autoTempMax->value());
    // Informar el motivo concreto, no solo la ruta. Evita interpretar
    // una temperatura alta como "aceptada" por un umbral de HR antiguo.
    QStringList fueraDeRango;
    if (avgHumidity < m_autoHumidityMin->value())
        fueraDeRango << "HR bajo el minimo";
    if (avgHumidity > m_autoHumidityMax->value())
        fueraDeRango << "HR sobre el maximo";
    if (avgTemperature < m_autoTempMin->value())
        fueraDeRango << "TEMPERATURA bajo el minimo";
    if (avgTemperature > m_autoTempMax->value())
        fueraDeRango << "TEMPERATURA sobre el maximo";
    const QString explanation = fueraDeRango.isEmpty()
        ? "AMBAS MEDICIONES DENTRO DE RANGO"
        : "FUERA POR: " + fueraDeRango.join(" Y ");
    const QString criteria = QString("HR entre %1 y %2 %, T entre %3 y %4 °C")
        .arg(m_autoHumidityMin->value()).arg(m_autoHumidityMax->value())
        .arg(m_autoTempMin->value(), 0, 'f', 1)
        .arg(m_autoTempMax->value(), 0, 'f', 1);
    const QString result = QString("MEDICION 10 s (HR+T v3): HR media %1 %, T media %2 °C "
                                   "(%3 muestras; %4) -> RUTA %5: %6 | %7")
       .arg(avgHumidity, 0, 'f', 1).arg(avgTemperature, 0, 'f', 1)
       .arg(m_humiditySamples.size()).arg(criteria).arg(chosen + 1)
       .arg(chosen == 2 ? "FUERA DE LIMITES" : "DENTRO DE LIMITES")
       .arg(explanation);
    m_autoSamplesLabel->setText(m_autoMeasurementOnly ? result :
        QString("BUCLE %1/%2 | %3").arg(m_autoCycleNumber).arg(m_autoRepeatTarget).arg(result));
    if (m_autoMeasurementOnly) {
        stopAuto(false, result + ". Solo medicion; no se movio el brazo.");
    } else {
        m_autoResult->setText(result + ". Iniciando la ruta grabada...");
        startAutoStage(chosen);
    }
}

void MainWindow::stopAuto(bool sendStop, const QString &reason) {
    if (!m_autoRunning) return;
    m_autoStepTimer.stop();
    m_autoMeasureTimer.stop();
    m_autoNextCycleTimer.stop();
    m_measureClock.invalidate();
    m_autoRunning = false;
    m_autoState = AutoState::Idle;
    m_autoActiveStage = -1;
    m_autoTestMode = false;
    m_autoMeasurementOnly = false;
    m_autoCycleNumber = 0;
    m_autoRepeatTarget = 1;
    if (sendStop && m_serial.isOpen()) sendLine("STOP");
    setAutoBusy(false);
    m_autoResult->setText(reason);
    setStatus(reason);
}

void MainWindow::configureAutoRepeats() {
    if (m_autoRunning) return;
    bool accepted = false;
    const int value = QInputDialog::getInt(this, "Definir bucles del ciclo",
        "Cuantas veces se ejecutara el ciclo completo (agarrar + medir 10 s + ruta)?\n"
        "1 = una sola ejecucion. Maximo 1000:",
        m_autoRepeatCount, 1, 1000, 1, &accepted);
    if (!accepted) return;
    m_autoRepeatCount = value;
    m_autoRepeatLabel->setText(QString("BUCLES CONFIGURADOS: %1").arg(value));
    m_autoResult->setText(QString("Se ejecutaran %1 bucles completos al pulsar INICIAR. "
        "Cada bucle incluye una nueva medicion del DHT22.").arg(value));
}

void MainWindow::saveAutoProfile() {
    writeAutoCycleJson("Guardar perfil de las 4 etapas", "ciclo_automatico.json");
}

void MainWindow::exportAutoCycle() {
    writeAutoCycleJson("Exportar ciclo completo", "ciclo_completo.json");
}

void MainWindow::writeAutoCycleJson(const QString &title, const QString &suggestedName) {
    if (m_autoRunning) return;
    const QString fileName = QFileDialog::getSaveFileName(this, title,
                        suggestedName, "Archivos JSON (*.json)");
    if (fileName.isEmpty()) return;
    static constexpr const char *keys[4] = {"pick", "measure_position", "humid", "dry"};
    QJsonObject root;
    // Formato v2: dos limites inclusivos para cada variable ambiental.
    // La importacion aun admite archivos v1 con un unico umbral de humedad.
    root.insert("format", "informatica2-dht22-auto");
    root.insert("version", 2);
    root.insert("step_duration_ms", m_duration->value());
    root.insert("humidity_min_percent", m_autoHumidityMin->value());
    root.insert("humidity_max_percent", m_autoHumidityMax->value());
    root.insert("temperature_min_c", m_autoTempMin->value());
    root.insert("temperature_max_c", m_autoTempMax->value());
    root.insert("repeat_count", m_autoRepeatCount);
    QJsonObject stages;
    for (int i=0; i<AutoStages; ++i) {
        QJsonArray poses;
        for (const QVector<int> &pose : m_autoSequences[i]) {
            QJsonArray one;
            for (int value : pose) one.append(value);
            poses.append(one);
        }
        stages.insert(QString::fromLatin1(keys[i]), poses);
    }
    root.insert("stages", stages);
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Error", "No fue posible abrir el archivo JSON: " + f.errorString());
        return;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size() || !f.flush()) {
        QMessageBox::critical(this, "Error", "No fue posible escribir el archivo JSON: " + f.errorString());
        f.close();
        return;
    }
    f.close();
    m_autoResult->setText(QString("Ciclo exportado: 4 etapas, %1 bucles, "
                                  "HR de %2 a %3 %, T de %4 a %5 °C.\n%6")
                          .arg(m_autoRepeatCount).arg(m_autoHumidityMin->value())
                          .arg(m_autoHumidityMax->value())
                          .arg(m_autoTempMin->value(), 0, 'f', 1)
                          .arg(m_autoTempMax->value(), 0, 'f', 1).arg(fileName));
}

void MainWindow::loadAutoProfile() {
    if (m_autoRunning) return;
    const QString name = QFileDialog::getOpenFileName(this, "Importar ciclo completo", QString(),
                                                      "Archivos JSON (*.json)");
    if (name.isEmpty()) return;
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024*1024) {
        QMessageBox::warning(this, "Error", "No se puede abrir el JSON o supera 1 MiB.");
        return;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, "JSON invalido", "El archivo no contiene un objeto JSON valido.");
        return;
    }
    const QJsonObject root = doc.object();
    const int formatVersion = root.value("version").toInt(-1);
    if (root.value("format").toString() != "informatica2-dht22-auto"
        || (formatVersion != 1 && formatVersion != 2) || !root.value("stages").isObject()) {
        QMessageBox::warning(this, "Formato invalido", "Este archivo no es un perfil de ciclo automatico compatible.");
        return;
    }
    const QJsonValue durationV = root.value("step_duration_ms");
    const QJsonValue repeatV = root.value("repeat_count");
    const QJsonValue hrMinV = root.value("humidity_min_percent");
    const QJsonValue hrMaxV = root.value("humidity_max_percent");
    const QJsonValue tempMinV = root.value("temperature_min_c");
    const QJsonValue tempMaxV = root.value("temperature_max_c");
    // Los JSON v1 tenian UN SOLO umbral HR, y el control T podia estar apagado.
    // Como la nueva consigna exige las DOS variables y dos rangos, al importar
    // v1 solo se conservan movimientos/bucles/tiempo; los rangos toman 40-70 y 15-30.
    const bool legacy = formatVersion == 1;
    const double hrMin = legacy ? 40.0 : hrMinV.toDouble(-1);
    const double hrMax = legacy ? 70.0 : hrMaxV.toDouble(-1);
    const double tempMin = legacy ? 15.0 : tempMinV.toDouble(-999);
    const double tempMax = legacy ? 30.0 : tempMaxV.toDouble(-999);
    const double duration = durationV.toDouble(-1);
    const double repeats = repeatV.isUndefined() ? 1 : repeatV.toDouble(-1);
    const QJsonValue oldThresholdV = root.value("humidity_threshold_percent");
    const double oldThreshold = oldThresholdV.toDouble(-1);
    if (!durationV.isDouble() || !std::isfinite(duration)
        || duration < 100 || duration > 10000 || duration != int(duration)
        || (!repeatV.isUndefined() && !repeatV.isDouble())
        || !std::isfinite(repeats) || repeats < 1 || repeats > 1000 || repeats != int(repeats)
        || (legacy && (!oldThresholdV.isDouble() || !std::isfinite(oldThreshold)
                       || oldThreshold < 1 || oldThreshold > 99 || oldThreshold != int(oldThreshold)))
        || (!legacy && (!hrMinV.isDouble() || !hrMaxV.isDouble()
            || !tempMinV.isDouble() || !tempMaxV.isDouble()
            || !std::isfinite(hrMin) || !std::isfinite(hrMax)
            || hrMin < 0 || hrMax > 100 || hrMin > hrMax
            || hrMin != int(hrMin) || hrMax != int(hrMax)
            || !std::isfinite(tempMin) || !std::isfinite(tempMax)
            || tempMin < -40.0 || tempMax > 80.0 || tempMin > tempMax))) {
        QMessageBox::warning(this, "Datos invalidos", "Tiempo, bucles o rangos ambientales invalidos.");
        return;
    }
    static constexpr const char *keys[4] = {"pick", "measure_position", "humid", "dry"};
    const QJsonObject stages = root.value("stages").toObject();
    QVector<QVector<int>> parsed[AutoStages];
    for (int i=0; i<AutoStages; ++i) {
        const QJsonValue v = stages.value(QString::fromLatin1(keys[i]));
        if (!v.isArray() || v.toArray().size() > 100) {
            QMessageBox::warning(this, "JSON invalido", "Cada etapa requiere un array con hasta 100 posturas.");
            return;
        }
        for (const QJsonValue &item : v.toArray()) {
            if (!item.isArray() || item.toArray().size() != ServoCount) {
                QMessageBox::warning(this, "Postura invalida", "Cada postura debe tener exactamente seis angulos.");
                return;
            }
            QVector<int> pose;
            for (const QJsonValue &angle : item.toArray()) {
                const double val = angle.toDouble(-1);
                if (!angle.isDouble() || val < 0 || val > 180 || val != int(val)) {
                    QMessageBox::warning(this, "Angulo invalido", "Angulos requeridos: enteros de 0 a 180.");
                    return;
                }
                pose.append(int(val));
            }
            parsed[i].append(pose);
        }
    }
    // Validamos TODO antes de sustituir el ciclo actual.
    // Si ya habia movimientos, confirmar para evitar perder trabajo sin exportar.
    bool hasExistingPositions = false;
    for (int i=0; i<AutoStages; ++i) {
        if (!m_autoSequences[i].isEmpty()) hasExistingPositions = true;
    }
    if (hasExistingPositions && QMessageBox::question(this, "Importar ciclo completo",
            "Se reemplazaran los movimientos de las cuatro etapas y los ajustes "
            "de tiempo, rangos de humedad y temperatura y bucles.\n\n"
            "Si no los exportaste, los cambios actuales se perderan. Continuar?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    // Importacion atomica: no alterar ningun paso si el archivo falla o se cancela.
    for (int i=0; i<AutoStages; ++i) m_autoSequences[i] = parsed[i];
    m_duration->setValue(int(duration));
    // Asignar primero un intervalo seguro para evitar ajustes cruzados
    // durante la restauracion (los widgets preservan minimo <= maximo).
    m_autoHumidityMin->setValue(0);
    m_autoHumidityMax->setValue(int(hrMax));
    m_autoHumidityMin->setValue(int(hrMin));
    m_autoTempMin->setValue(-40.0);
    m_autoTempMax->setValue(tempMax);
    m_autoTempMin->setValue(tempMin);
    m_autoRepeatCount = int(repeats);
    m_autoRepeatLabel->setText(QString("BUCLES CONFIGURADOS: %1").arg(m_autoRepeatCount));
    updateAutoLists();
    m_autoResult->setText(QString("CICLO IMPORTADO: 4 etapas, %1 bucles, "
                                  "HR de %2 a %3 %, T de %4 a %5 °C, %6 ms/paso.\n%7%8")
                          .arg(m_autoRepeatCount).arg(m_autoHumidityMin->value())
                          .arg(m_autoHumidityMax->value())
                          .arg(m_autoTempMin->value(), 0, 'f', 1)
                          .arg(m_autoTempMax->value(), 0, 'f', 1)
                          .arg(m_duration->value()).arg(name)
                          .arg(legacy ? "\nPerfil antiguo: se aplicaron los NUEVOS rangos 40–70 % y 15–30 °C." : ""));
    setStatus("Ciclo completo importado en Qt. No se movieron los servomotores.");
}
