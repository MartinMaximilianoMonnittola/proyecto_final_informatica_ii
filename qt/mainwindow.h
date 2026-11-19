#pragma once
#include <QMainWindow>
#include <QVector>
#include <QByteArray>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>

class QGraphicsScene;
class QGraphicsView;
class QComboBox;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QListWidget;
class QLabel;
class JointBar;
class QTabWidget;
class QGroupBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    enum class Protocol { Info2, Legacy };
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QTabWidget *m_tabs = nullptr;
    QSerialPort m_serial;
    QByteArray m_inputBuffer;      // solo bytes de la linea parcial actual
    bool m_discardingSerialLine = false;
    QStringList m_serialHistory;
    QString m_lastCommand;
    QComboBox *m_ports = nullptr;
    QComboBox *m_protocol = nullptr;
    QPushButton *m_connect = nullptr;
    QLabel *m_connectionState = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_dhtTemperatura = nullptr;
    QLabel *m_dhtHumedad = nullptr;
    QLabel *m_dhtEstado = nullptr;
    QTimer m_dhtTimeout;  // sin telemetria durante 8 s => estado desconocido
    QLabel *m_sequenceCount = nullptr;
    QSpinBox *m_duration = nullptr;
    QCheckBox *m_repeat = nullptr;
    QListWidget *m_records = nullptr;
    QVector<JointBar*> m_joints;
    QVector<QVector<int>> m_poses;
    QTimer m_playbackTimer;
    QTimer m_jointDebounce[6];
    bool m_playing = false;
    int m_playbackIndex = 0;
    bool m_receivingState = false;

    // Ciclo automático: 0 tomar, 1 posicionar para medir, 2 ambiente FUERA, 3 ambiente DENTRO.
    static constexpr int AutoStages = 4;
    enum class AutoState { Idle, Picking, Positioning, Measuring, WetRoute, DryRoute, Testing };
    AutoState m_autoState = AutoState::Idle;
    QVector<QVector<int>> m_autoSequences[AutoStages];
    QListWidget *m_autoLists[AutoStages] = {};
    QGroupBox *m_autoCards[AutoStages] = {};
    QLabel *m_autoResult = nullptr;
    QLabel *m_autoSamplesLabel = nullptr;
    QLabel *m_autoRouteDescriptions[2] = {}; // textos vivos de las rutas 3 y 4
    QPushButton *m_autoStart = nullptr;
    QPushButton *m_autoStop = nullptr;
    QPushButton *m_autoMeasureOnlyButton = nullptr;
    QPushButton *m_autoLoad = nullptr;
    QPushButton *m_autoSave = nullptr;
    QPushButton *m_autoExport = nullptr;
    QPushButton *m_autoRepeatButton = nullptr;
    QLabel *m_autoRepeatLabel = nullptr;
    QSpinBox *m_autoHumidityMin = nullptr;
    QSpinBox *m_autoHumidityMax = nullptr;
    QDoubleSpinBox *m_autoTempMin = nullptr;
    QDoubleSpinBox *m_autoTempMax = nullptr;
    QTimer m_autoStepTimer;
    QTimer m_autoMeasureTimer;
    QTimer m_autoNextCycleTimer;
    QElapsedTimer m_measureClock;
    QVector<double> m_humiditySamples;
    QVector<double> m_temperatureSamples;
    int m_autoActiveStage = -1;
    int m_autoNextIndex = 0;
    bool m_autoRunning = false;
    bool m_autoTestMode = false;
    bool m_autoMeasurementOnly = false;
    int m_autoRepeatCount = 1;   // cantidad configurada en Qt
    int m_autoRepeatTarget = 1;  // copia fija para la ejecucion en curso
    int m_autoCycleNumber = 0;   // ciclo actual, comenzando en 1


    QWidget *place(QWidget *widget, int x, int y, int w, int h);
    QPushButton *makeButton(const QString &caption, int x, int y, int w, int h,
                            const QString &objectName="action");
    QLabel *makeLabel(const QString &caption, int x, int y, int w, int h,
                      const QString &objectName="info");
    void buildUi();
    void refreshPorts();
    void toggleConnection();
    void disconnectSerial();
    bool sendLine(const QString &line);
    void sendSingleJoint(int index);
    bool sendFullPose(const QVector<int> &pose, int milliseconds);
    Protocol protocol() const;
    QVector<int> currentPose() const;
    void setPose(const QVector<int> &pose);
    void setStatus(const QString &message);
    void processInput();
    void processLine(const QString &line);
    void logSerial(const QString &direction, const QString &line);
    void showSerialDiagnostic();
    void setDhtSinDatos(const QString &motivo);
    void recordPose();
    void updateRecordList();
    void startPlayback();
    void playNext();
    void stopPlayback(bool sendStop = true);
    void homePose();
    void clearSequence();
    void exportSequence();
    void importSequence();
    void openLimitsDialog();

    void buildAutoUi();
    void updateAutoRouteDescriptions();
    void updateAutoLists();
    void addAutoPose(int stage);
    void deleteAutoPose(int stage);
    void clearAutoStage(int stage);
    bool validateAutoPoses(int stage) const;
    void startAutoCycle();
    void testAutoStage(int stage);
    void startMeasurementOnly();
    void startAutoStage(int stage);
    void sendAutoStep();
    void completeAutoStage();
    void beginAutoMeasurement();
    void updateAutoMeasurement();
    void stopAuto(bool sendStop, const QString &reason);
    void setAutoBusy(bool busy);
    void configureAutoRepeats();
    void exportAutoCycle();
    void writeAutoCycleJson(const QString &title, const QString &suggestedName);
    void saveAutoProfile();
    void loadAutoProfile();
};