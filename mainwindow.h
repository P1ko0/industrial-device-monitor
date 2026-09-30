#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QString>
#include <QTcpSocket>
#include <QByteArray>
#include <QModbusTcpClient>
#include "Device.h"
#include "Simulator.h"
#include "Logger.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    QTimer *timer;
    QTcpSocket *socket = nullptr;
    QByteArray receiveBuffer;
    Device device;
    Simulator simulator;
    bool saveParameters();
    bool loadParameters();
    void appendLog(const QString& message,LogLevel level = LogLevel::Info);
    void refreshDeviceDisplay();
    QByteArray encodeRequest(
        const QString& command,
        int deviceId,
        const QString& value
        );
    QTimer *reconnectTimer = nullptr;
    QTimer *connectTimeoutTimer = nullptr;
    void connectToServer();
    void scheduleReconnect();
    int remoteSpeed = 0;
    int remoteTemperature = 0;
    QString remoteStatus = "unknown";
    void handleResponse(const QString& response);
    bool faultAlarmRecorded = false;
    QModbusTcpClient *modbusClient = nullptr;
    void readModbusData();
    bool initAlarmDatabase();
    bool saveAlarm(const QString &time,int device_id,const QString &fault_code,const QString &description,qint64 &alarmId);
    bool loadAlarmHistory();
    bool acknowledgeAlarm(qint64 alarmId);
};
#endif // MAINWINDOW_H
