#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "Logger.h"
#include <QDateTime>
#include <QTableWidgetItem>
#include <QIntValidator>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QSaveFile>
#include <QFile>
#include <QStringList>
#include <QModbusDevice>
#include <QVariant>
#include <QModbusDataUnit>
#include <QModbusReply>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if(initAlarmDatabase())
    {
        appendLog("报警数据库初始化成功");
        if(loadAlarmHistory())
        {
            appendLog("历史报警加载成功");
        }
        else
        {
            appendLog("历史报警加载失败");
        }
    }
    else
    {
        appendLog("报警历史保存不可用");
    }

    modbusClient = new QModbusTcpClient(this);
    modbusClient->setConnectionParameter(
        QModbusDevice::NetworkAddressParameter,
        QStringLiteral("127.0.0.1")
        );

    modbusClient->setConnectionParameter(
        QModbusDevice::NetworkPortParameter,
        1502
        );

    connect(
        modbusClient,
        &QModbusDevice::stateChanged,
        this,
        [this](QModbusDevice::State state)
        {
            if (state == QModbusDevice::ConnectedState)
            {
                appendLog("Modbus：连接成功");
                readModbusData();
            }
            else if (state == QModbusDevice::UnconnectedState)
            {
                appendLog("Modbus：未连接",LogLevel::Warning);
            }
        }
        );

    connect(
        modbusClient,
        &QModbusDevice::errorOccurred,
        this,
        [this](QModbusDevice::Error error)
        {
            if (error != QModbusDevice::NoError)
            {
                appendLog("Modbus错误：" + modbusClient->errorString());
            }
        }
        );

    if (!modbusClient->connectDevice())
    {
        appendLog("Modbus：无法发起连接，"
                  + modbusClient->errorString());
    }

    socket = new QTcpSocket(this);
    // 连接失败或断开后，等待3秒再重试
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(3000);
    reconnectTimer->setSingleShot(true);

    // 每次建立连接，最多等待5秒
    connectTimeoutTimer = new QTimer(this);
    connectTimeoutTimer->setInterval(5000);
    connectTimeoutTimer->setSingleShot(true);

    connect(
        socket,
        &QTcpSocket::connected,
        this,
        [this]
        {
            connectTimeoutTimer->stop();
            reconnectTimer->stop();

            ui->connectionLabel->setText("网络：已连接");

            appendLog("服务器连接成功");

            if (socket->write(encodeRequest("GET_STATUS", 1, "")) == -1)
            {
                appendLog("查询发送失败：" + socket->errorString());
                return;
            }

            appendLog("状态查询命令已提交发送");
        }
        );

    connect(
        socket,
        &QTcpSocket::disconnected,
        this,
        [this]
        {
            appendLog("服务器连接已断开");
            scheduleReconnect();
        }
        );

    connect(
        socket,
        &QTcpSocket::errorOccurred,
        this,
        [this]
        {
            appendLog("网络错误：" + socket->errorString(),LogLevel::Warning);

            if (socket->state() != QAbstractSocket::ConnectedState)
            {
                scheduleReconnect();
            }
        }
        );

    connect(
        socket,
        &QTcpSocket::readyRead,
        this,
        [this]
        {
            // 把本次收到的数据追加到缓冲区
            receiveBuffer += socket->readAll();

            // 有换行符，说明至少有一条完整消息
            while (receiveBuffer.contains('\n'))
            {
                auto index = receiveBuffer.indexOf('\n');

                // 取出换行符前面的消息内容
                QByteArray message = receiveBuffer.left(index);

                // 删除已取出的消息，包括末尾换行符
                receiveBuffer.remove(0, index + 1);

                if(!message.isEmpty())
                {
                    QString response = QString::fromUtf8(message);

                    appendLog("收到：" + response);
                    handleResponse(response);
                }
            }
        }
        );

    connect(
        reconnectTimer,
        &QTimer::timeout,
        this,
        [this]
        {
            connectToServer();
        }
        );

    connect(
        connectTimeoutTimer,
        &QTimer::timeout,
        this,
        [this]
        {
            auto state = socket->state();

            // 只取消仍在建立过程中的连接
            if (state != QAbstractSocket::HostLookupState
                && state != QAbstractSocket::ConnectingState)
            {
                return;
            }

            appendLog("连接超时，取消本次连接");

            socket->abort();

            scheduleReconnect();
        }
        );

    connectToServer();

    QIntValidator* speedValidator = new QIntValidator(0, 3000, this);
    ui->speedEdit->setValidator(speedValidator);

    QIntValidator* tempValidator = new QIntValidator(1, 80, this);
    ui->tempLimitEdit->setValidator(tempValidator);

    // 有配置文件时，尝试读取
    if (QFile::exists("config.json") && !loadParameters())
    {
        QMessageBox::warning(
            this,
            "配置读取失败",
            "配置文件无法读取、格式错误或参数不合法，使用默认参数。"
            );
    }

    // 把设备中的参数显示到输入框
    ui->speedEdit->setText(
        QString::number(device.getSpeedSetpoint())
        );

    ui->tempLimitEdit->setText(
        QString::number(device.getTempLimit())
        );

    ui->tableDevice->setRowCount(1);

    refreshDeviceDisplay();

    timer = new QTimer(this);

    // 更新显示与模拟机数据
    connect(
        timer,
        &QTimer::timeout,
        this,
        [this]
        {
            simulator.update();
            refreshDeviceDisplay();
        }
        );

        timer->start(500);

    // 通信方式改变信号
    connect(
        ui->communicationModeComboBox,
        &QComboBox::currentIndexChanged,
        this,
        [this](int index)
        {
            refreshDeviceDisplay();

            if(index == 0)
            {
                if(socket->state() != QAbstractSocket::ConnectedState)
                {
                    appendLog("状态查询失败：文本 TCP 尚未连接");
                    return;
                }

                if(socket->write(encodeRequest("GET_STATUS", 1, "")) == -1)
                {
                    appendLog("查询发送失败：" + socket->errorString());
                    return;
                }

                appendLog("状态查询命令已提交发送");
            }
            else if(index == 1)
            {
                if (modbusClient->state() != QModbusDevice::ConnectedState)
                {
                    appendLog("状态查询失败：Modbus 尚未连接");
                    return;
                }

                QModbusDataUnit request(
                    QModbusDataUnit::InputRegisters,
                    0,
                    3
                    );

                QModbusReply *reply = modbusClient->sendReadRequest(request, 1);

                if (reply == nullptr)
                {
                    appendLog("Modbus请求提交失败："
                              + modbusClient->errorString());
                    return;
                }

                auto handleReply = [this, reply]
                {
                    int mode = ui->communicationModeComboBox->currentIndex();
                    if(mode != 1)
                    {
                        reply->deleteLater();
                        return;
                    }
                    if (reply->error() != QModbusDevice::NoError)
                    {
                        appendLog("Modbus读取失败：" + reply->errorString());
                    }
                    else
                    {
                        QModbusDataUnit result = reply->result();

                        if (result.valueCount() != 3)
                        {
                            appendLog("Modbus读取失败：返回数量不符");
                        }
                        else
                        {
                            int speed = result.value(0);
                            remoteSpeed = speed;

                            int temperature = result.value(1);
                            remoteTemperature = temperature;

                            int status = result.value(2);

                            if(status == 0)
                            {
                                remoteStatus = "idle";
                            }
                            else if(status == 1)
                            {
                                remoteStatus = "running";
                            }
                            else if(status == 2)
                            {
                                remoteStatus = "stopped";
                            }
                            else if(status == 3)
                            {
                                remoteStatus = "error";
                            }
                            else
                            {
                                appendLog("未知 Modbus 状态码：" + QString::number(status));
                            }

                            if(status >= 0 && status <= 3)
                            {
                                refreshDeviceDisplay();
                                appendLog(
                                    "Modbus读取成功：状态码="
                                    + QString::number(status)
                                    );
                            }
                        }
                    }

                    reply->deleteLater();
                };

                if (reply->isFinished())
                {
                    handleReply();
                }
                else
                {
                    connect(
                        reply,
                        &QModbusReply::finished,
                        this,
                        handleReply
                        );
                }
            }
        }
        );

    // 启动按钮
    connect(
        ui->startButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            // 选择通信方式
            int mode = ui->communicationModeComboBox->currentIndex();
            if(mode == 0)
            {
                // 文本TCP方式
                if (socket->state() != QAbstractSocket::ConnectedState)
                {
                    appendLog("发送失败：尚未连接服务器");
                    return;
                }

                if (socket->write(encodeRequest("START", 1, "")) == -1)
                {
                    appendLog("发送失败：" + socket->errorString());
                    return;
                }

                appendLog("START 命令已提交发送");
            }
            else if(mode == 1)
            {
                // modbusTCP方式
                if(modbusClient->state() != QModbusDevice::ConnectedState)
                {
                    appendLog("启动失败：Modbus尚未连接");
                    return;
                }

                QModbusDataUnit startCoil(QModbusDataUnit::Coils, 0, 1);
                startCoil.setValue(0,1);

                QModbusReply *writeReply = modbusClient->sendWriteRequest(startCoil, 1);
                if(!writeReply)
                {
                    appendLog("启动写入请求发送失败：" + modbusClient->errorString());
                    return;
                }

                auto handleWriteReply = [this, writeReply]
                {
                    if(writeReply->error() != QModbusDevice::NoError)
                    {
                        appendLog("线圈写入失败：" + writeReply->errorString());
                    }
                    else
                    {
                        appendLog("启动线圈写入成功，待确认设备状态");

                        QModbusDataUnit request(
                            QModbusDataUnit::InputRegisters,
                            2,
                            1
                            );

                        // 1是服务端的设备编号
                        QModbusReply *reply = modbusClient->sendReadRequest(request, 1);

                        if (reply == nullptr)
                        {
                            appendLog("Modbus请求提交失败："
                                      + modbusClient->errorString());
                            writeReply->deleteLater();
                            return;
                        }

                        auto handleReply = [this, reply]
                        {
                            int mode = ui->communicationModeComboBox->currentIndex();
                            if(mode == 0)
                            {
                                reply->deleteLater();
                                return;
                            }
                            if (reply->error() != QModbusDevice::NoError)
                            {
                                appendLog("Modbus读取失败：" + reply->errorString());
                            }
                            else
                            {
                                QModbusDataUnit result = reply->result();

                                if (result.valueCount() != 1)
                                {
                                    appendLog("Modbus读取失败：返回数量不符");
                                }
                                else
                                {
                                    //int speed = result.value(0);
                                    //int temperature = result.value(1);
                                    int status = result.value(0);

                                    if(status == 0)
                                    {
                                        remoteStatus = "idle";
                                    }
                                    else if(status == 1)
                                    {
                                        remoteStatus = "running";
                                    }
                                    else if(status == 2)
                                    {
                                        remoteStatus = "stopped";
                                    }
                                    else if(status == 3)
                                    {
                                        remoteStatus = "error";
                                    }
                                    else
                                    {
                                        appendLog("未知 Modbus 状态码：" + QString::number(status));
                                    }

                                    if(status >= 0 && status <= 3)
                                    {
                                        refreshDeviceDisplay();
                                        appendLog(
                                            "Modbus读取成功：状态码="
                                            + QString::number(status)
                                            );
                                    }
                                }
                            }

                            reply->deleteLater();
                        };

                        if (reply->isFinished())
                        {
                            handleReply();
                        }
                        else
                        {
                            connect(
                                reply,
                                &QModbusReply::finished,
                                this,
                                handleReply
                                );
                        }
                    }

                    writeReply->deleteLater();
                };

                if(writeReply->isFinished())
                {
                    handleWriteReply();
                }
                else
                {
                    connect(
                        writeReply,
                        &QModbusReply::finished,
                        this,
                        handleWriteReply
                        );
                }
            }
            else
            {
                appendLog("请选择正确通信方式");
                return;
            }
        }
        );

        connect(
            ui->stopButton,
            &QPushButton::clicked,
            this,
            [this]
            {
                int mode = ui->communicationModeComboBox->currentIndex();
                if(mode == 0)
                {
                    if (socket->state() != QAbstractSocket::ConnectedState)
                    {
                        appendLog("发送失败：尚未连接服务器");
                        return;
                    }

                    if (socket->write(encodeRequest("STOP", 1, "")) == -1)
                    {
                        appendLog("发送失败：" + socket->errorString());
                        return;
                    }

                    appendLog("STOP 命令已提交发送");
                }
                else
                {
                    appendLog("当前模式暂不支持，请切换到文本 TCP");
                    return;
                }
            }
            );

    connect(
        ui->resetButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            int mode = ui->communicationModeComboBox->currentIndex();
            if(!mode)
            {
                if (socket->state() != QAbstractSocket::ConnectedState)
                {
                    appendLog("发送失败：尚未连接服务器");
                    return;
                }

                if (socket->write(encodeRequest("RESET", 1, "")) == -1)
                {
                    appendLog("发送失败：" + socket->errorString());
                    return;
                }

                appendLog("RESET 命令已提交发送");
            }
            else
            {
                appendLog("当前模式暂不支持，请切换到文本 TCP");
                return;
            }
        }
        );

        connect(
            ui->faultButton,
            &QPushButton::clicked,
            this,
            [this]
            {
                int mode = ui->communicationModeComboBox->currentIndex();
                if(!mode)
                {
                    if (socket->state() != QAbstractSocket::ConnectedState)
                    {
                        appendLog("发送失败：尚未连接服务器");
                        return;
                    }

                    QByteArray data = encodeRequest("FAULT", 1, "");

                    if (socket->write(data) == -1)
                    {
                        appendLog("发送失败：" + socket->errorString());
                        return;
                    }

                    appendLog("故障命令已提交发送");
                }
                else
                {
                    appendLog("当前模式暂不支持，请切换到文本 TCP");
                    return;
                }
            }
            );

    connect(
        ui->ackButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            int row = ui->tableAlarm->currentRow();

            if (row == -1)
            {
                return;
            }

            QTableWidgetItem *timeItem = ui->tableAlarm->item(row, 0);
            if(!timeItem)
            {
                appendLog("确认失败：报警时间单元格不存在");
                return;
            }

            bool idOK = false;

            qint64 alarmId = timeItem->data(Qt::UserRole).toLongLong(&idOK);
            if(!idOK)
            {
                appendLog("确认失败：报警编号无效");
                return;
            }

            QTableWidgetItem *ackItem = ui->tableAlarm->item(row, 2);
            if(!ackItem)
            {
                appendLog("确认失败：确认单元格不存在");
                return;
            }

            if(!acknowledgeAlarm(alarmId))
            {
                return;
            }

            ackItem->setText("已确认");

            appendLog("报警已确认");
        }
        );

    connect(
        ui->applyButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            // 读取速度
            bool speedOk = false;
            int speed = ui->speedEdit->text().toInt(&speedOk);

            if (!speedOk || !ui->speedEdit->hasAcceptableInput())
            {
                appendLog("本地参数应用被拒绝：速度必须为 0～3000 的整数",LogLevel::Warning);

                QMessageBox::warning(
                    this,
                    "参数错误",
                    "速度请输入0～3000的整数。"
                    );
                return;
            }

            // 读取温度上限
            bool tempOk = false;
            int limit = ui->tempLimitEdit->text().toInt(&tempOk);

            if (!tempOk || !ui->tempLimitEdit->hasAcceptableInput())
            {
                appendLog("本地参数应用被拒绝：温度上限必须为 1～80 的整数",LogLevel::Warning);

                QMessageBox::warning(
                    this,
                    "参数错误",
                    "温度上限请输入1～80的整数。"
                    );
                return;
            }

            // 两个输入都通过后，交给设备统一设置
            bool success = device.setParameters(speed, limit);

            if (!success)
            {
                QMessageBox::warning(
                    this,
                    "设置失败",
                    "参数超出设备允许的范围，原参数保持不变。"
                    );
                return;
            }

            if (!saveParameters())
            {
                QMessageBox::warning(
                    this,
                    "保存失败",
                    "参数已在当前程序中应用，但未能保存到config.json。"
                    );
                return;
            }

            // 显示设备中实际保存的参数
            QMessageBox::information(
                this,
                "设置成功",
                "速度设定："
                    + QString::number(device.getSpeedSetpoint())
                    + "\n温度上限："
                    + QString::number(device.getTempLimit())
                    + " ℃"
                );
        }
        );

    connect(
        ui->sendSpeedButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            int mode = ui->communicationModeComboBox->currentIndex();
            if(!mode)
            {
                if (socket->state() != QAbstractSocket::ConnectedState)
                {
                    appendLog("发送失败：尚未连接服务器");
                    return;
                }

                bool speedOk = false;
                int speed = ui->speedEdit->text().toInt(&speedOk);

                if (!speedOk || !ui->speedEdit->hasAcceptableInput())
                {
                    QMessageBox::warning(
                        this,
                        "参数错误",
                        "速度请输入0～3000的整数。"
                        );
                    return;
                }

                QByteArray data = encodeRequest(
                    "SET_SPEED",
                    1,
                    QString::number(speed)
                    );

                if (socket->write(data) == -1)
                {
                    appendLog("发送失败：" + socket->errorString());
                    return;
                }

                appendLog("速度设置命令已提交发送："
                          + QString::number(speed));
            }
            else
            {
                appendLog("当前模式暂不支持，请切换到文本 TCP");
                return;
            }
        }
        );
}

bool MainWindow::saveParameters()
{
    // 整理设备当前的参数
    QJsonObject config;
    config["speedSetpoint"] = device.getSpeedSetpoint();
    config["tempLimit"] = device.getTempLimit();

    // 转换成可以写入文件的JSON数据
    QJsonDocument document(config);
    QByteArray data = document.toJson();

    // 打开文件，准备保存
    QSaveFile file("config.json");

    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    // 检查数据是否完整写入
    if (file.write(data) != data.size())
    {
        return false;
    }

    // 完成文件保存，并返回结果
    return file.commit();
}

bool MainWindow::loadParameters()
{
    QFile file("config.json");

    // 以只读方式打开文件
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    // 读取文件内容
    QByteArray data = file.readAll();

    if (file.error() != QFileDevice::NoError)
    {
        return false;
    }

    // 将文件内容解析成JSON文档
    QJsonDocument document = QJsonDocument::fromJson(data);

    if (!document.isObject())
    {
        return false;
    }

    // 按名称取出参数
    QJsonObject config = document.object();

    int speed = config.value("speedSetpoint").toInt(-1);
    int limit = config.value("tempLimit").toInt(-1);

    // 复用设备的校验规则，统一应用参数
    return device.setParameters(speed, limit);
}

void MainWindow::appendLog(const QString& message,LogLevel level)
{
    QString levelText = "INFO";
    if(level == LogLevel::Error)
    {
        levelText = "ERROR";
    }
    else if(level == LogLevel::Warning)
    {
        levelText = "WARN";
    }

    QString time = QDateTime::currentDateTime()
    .toString("yyyy-MM-dd hh:mm:ss");

    ui->logEdit->appendPlainText(time + " [" + levelText +"] " + message);

    if(!Logger::write(message.toStdString(),level))
    {
        ui->logEdit->appendPlainText("[ERROR] 日志文件写入失败，本条日志仅显示在界面");
    }
}

void MainWindow::refreshDeviceDisplay()
{
    QString statusText = remoteStatus;

    ui->statusLabel->setText(statusText);

    ui->tableDevice->setItem(
        0, 0, new QTableWidgetItem("Device1")
        );

    ui->tableDevice->setItem(
        0, 1, new QTableWidgetItem(statusText)
        );

    int index = ui->communicationModeComboBox->currentIndex();

    if(index == 0)
    {
        ui->tableDevice->setItem(
            0, 2,
            new QTableWidgetItem(
                "-"
                )
            );

        ui->tableDevice->setItem(
            0, 3,
            new QTableWidgetItem(
                "-"
                )
            );
    }
    else
    {
        ui->tableDevice->setItem(
            0, 2,
            new QTableWidgetItem(
                QString::number(remoteTemperature)
                )
            );

        ui->tableDevice->setItem(
            0, 3,
            new QTableWidgetItem(
                QString::number(remoteSpeed)
                )
            );
    }
}

QByteArray MainWindow::encodeRequest(
    const QString& command,
    int deviceId,
    const QString& value
    )
{
    QString message = command
                      + "|"
                      + QString::number(deviceId)
                      + "|"
                      + value
                      + "\n";

    return message.toUtf8();
}

void MainWindow::connectToServer()
{
    // 只有未连接时，才允许发起新的连接
    if (socket->state() != QAbstractSocket::UnconnectedState)
    {
        return;
    }

    reconnectTimer->stop();

    // 新连接不沿用旧连接留下的不完整消息
    receiveBuffer.clear();

    ui->connectionLabel->setText("网络：连接中");

    appendLog("正在连接服务器……");

    // 先开始计时，再发起连接
    connectTimeoutTimer->start();

    socket->connectToHost("127.0.0.1", 5000);
}

void MainWindow::scheduleReconnect()
{
    remoteStatus = "unknown";
    refreshDeviceDisplay();

    ui->connectionLabel->setText("网络：未连接，等待重试");

    connectTimeoutTimer->stop();

    if (reconnectTimer->isActive())
    {
        return;
    }

    appendLog("将在3秒后重新连接");
    reconnectTimer->start();
}

void MainWindow::handleResponse(const QString& response)
{
    int mode = ui->communicationModeComboBox->currentIndex();
    if(mode)
    {
        return;
    }
    QStringList fields = response.split('|', Qt::KeepEmptyParts);

    QString status;

    if (fields.size() == 2
        && (fields[0] == "STATE" || fields[0] == "OK"))
    {
        status = fields[1];
    }
    else if (fields.size() == 3 && fields[0] == "ERR")
    {
        status = fields[2];
    }
    else
    {
        return;
    }

    // 只接受设备状态，不能把速度值当成状态
    if (status != "idle"
        && status != "running"
        && status != "stopped"
        && status != "error")
    {
        return;
    }

    if (status == "error")
    {
        if (!faultAlarmRecorded)
        {
            QString time = QDateTime::currentDateTime()
            .toString("yyyy-MM-dd hh:mm:ss");

            qint64 alarmId = 0;

            bool saved = saveAlarm(time,1,"DEVICE_FAULT","服务端设备发生故障",alarmId);
            if(saved)
            {
                appendLog("报警已保存");
            }
            else
            {
                appendLog("本条报警未保存或者编号获取失败");
            }

            int row = ui->tableAlarm->rowCount();
            ui->tableAlarm->insertRow(row);

            QTableWidgetItem *timeItem = new QTableWidgetItem(time);
            if(saved)
            {
                timeItem->setData(Qt::UserRole,alarmId);
            }
            ui->tableAlarm->setItem(
                row, 0, timeItem
                );

            ui->tableAlarm->setItem(
                row, 1, new QTableWidgetItem("服务端设备发生故障")
                );

            ui->tableAlarm->setItem(
                row, 2, new QTableWidgetItem("未确认")
                );

            appendLog("服务端设备发生故障");

            faultAlarmRecorded = true;
        }
    }
    else
    {
        // 收到明确的非故障状态，允许记录下一次故障
        faultAlarmRecorded = false;
    }

    remoteStatus = status;
    refreshDeviceDisplay();
}

void MainWindow::readModbusData()
{
    if (modbusClient->state() != QModbusDevice::ConnectedState)
    {
        appendLog("Modbus读取失败：尚未连接");
        return;
    }

    // 保持寄存器，地址从0开始，读取2个
    QModbusDataUnit hr_request(
        QModbusDataUnit::HoldingRegisters,
        0,
        2
        );

    QModbusReply *hr_reply = modbusClient->sendReadRequest(hr_request, 1);

    if (hr_reply == nullptr)
    {
        appendLog("保持寄存器读取请求发送失败："
                  + modbusClient->errorString());
        return;
    }

    auto handleHoldingReply = [this, hr_reply]
    {
        if(hr_reply->error() != QModbusDevice::NoError)
        {
            appendLog("保持寄存器读取失败:" + hr_reply->errorString());
        }
        else
        {
            QModbusDataUnit result = hr_reply->result();
            if(result.valueCount() != 2)
            {
                appendLog("保持寄存器返回数量异常");
            }
            else
            {
                int setSpeedpoint = result.value(0);
                int setTemplimit = result.value(1);

                ui->modbusDataLabel->setText("Modbus速度设定值：" + QString::number(setSpeedpoint) + "rpm\n"+
                                             "温度上限：" + QString::number(setTemplimit) + "℃");

                appendLog(
                    "Modbus读取成功：速度设定值="
                    + QString::number(setSpeedpoint)
                    + " rpm，温度上限="
                    + QString::number(setTemplimit)
                    + " ℃"
                    );
            }
        }
        hr_reply->deleteLater();
    };

    if(hr_reply->isFinished())
    {
        handleHoldingReply();
    }
    else
    {
        connect(
            hr_reply,
            &QModbusReply::finished,
            this,
            handleHoldingReply
            );
    }

    // 输入寄存器，从地址0开始，读取3个
    QModbusDataUnit request(
        QModbusDataUnit::InputRegisters,
        0,
        3
        );

    // 1是服务端的设备编号
    QModbusReply *reply = modbusClient->sendReadRequest(request, 1);

    if (reply == nullptr)
    {
        appendLog("Modbus请求提交失败："
                  + modbusClient->errorString());
        return;
    }

    auto handleReply = [this, reply]
    {
        if (reply->error() != QModbusDevice::NoError)
        {
            appendLog("Modbus读取失败：" + reply->errorString());
        }
        else
        {
            QModbusDataUnit result = reply->result();

            if (result.valueCount() != 3)
            {
                appendLog("Modbus读取失败：返回数量不符");
            }
            else
            {
                int speed = result.value(0);
                int temperature = result.value(1);
                int status = result.value(2);

                ui->modbusDataLabel->setText("Modbus速度：" + QString::number(speed) + "rpm\n"+
                                             "温度：" + QString::number(temperature) + "℃\n"+
                                             "状态码：" + QString::number(status));

                appendLog(
                    "Modbus读取成功：速度="
                    + QString::number(speed)
                    + " rpm，温度="
                    + QString::number(temperature)
                    + " ℃，状态码="
                    + QString::number(status)
                    );
            }
        }

        reply->deleteLater();
    };

    if (reply->isFinished())
    {
        handleReply();
    }
    else
    {
        connect(
            reply,
            &QModbusReply::finished,
            this,
            handleReply
            );
    }
}

bool MainWindow::initAlarmDatabase()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("alarm.db");
    if(!db.open())
    {
        appendLog("打开数据库失败" + db.lastError().text(),LogLevel::Error);

        return false;
    }

    QSqlQuery query(db);

    QString sql = "CREATE TABLE IF NOT EXISTS alarms(id INTEGER PRIMARY KEY,time TEXT NOT NULL,device_id INTEGER NOT NULL,fault_code TEXT NOT NULL,description TEXT NOT NULL,acknowledged INTEGER NOT NULL DEFAULT 0)";

    if(!query.exec(sql))
    {
        appendLog("创建报警表失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    return true;
}

bool MainWindow::saveAlarm(const QString &time,int device_id,const QString &fault_code,const QString &description,qint64 &alarmId)
{
    alarmId = 0;
    bool idOK = false;

    QSqlQuery query;

    QString sql = "INSERT INTO alarms(time,device_id,fault_code,description) VALUES(:time,:device_id,:fault_code,:description)";

    if(!query.prepare(sql))
    {
        appendLog("准备报警写入失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    query.bindValue(":time",time);
    query.bindValue(":device_id",device_id);
    query.bindValue(":fault_code",fault_code);
    query.bindValue(":description",description);

    if(!query.exec())
    {
        appendLog("保存报警失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    alarmId = query.lastInsertId().toLongLong(&idOK);
    if(!idOK)
    {
        appendLog("报警已写入，但获取编号失败",LogLevel::Error);
        return false;
    }

    return true;
}

bool MainWindow::loadAlarmHistory()
{
    QSqlQuery query;

    QString sql = "SELECT id,time,description,acknowledged FROM alarms ORDER BY id ASC";

    if(!query.exec(sql))
    {
        appendLog("读取历史报警失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    ui->tableAlarm->setRowCount(0);
    while(query.next())
    {
        qint64 alarmId = query.value(0).toLongLong();
        QString time = query.value(1).toString();
        QString description = query.value(2).toString();
        int acknowledged = query.value(3).toInt();

        int row = ui->tableAlarm->rowCount();
        ui->tableAlarm->insertRow(row);

        QString ack;
        if(acknowledged)
        {
            ack = "已确认";
        }
        else
        {
            ack = "未确认";
        }

        QTableWidgetItem *timeItem = new QTableWidgetItem(time);
        timeItem->setData(Qt::UserRole,alarmId);
        ui->tableAlarm->setItem(row,0,timeItem);

        QTableWidgetItem *decriptionItem = new QTableWidgetItem(description);
        ui->tableAlarm->setItem(row,1,decriptionItem);

        QTableWidgetItem *acknowlegedItem = new QTableWidgetItem(ack);
        ui->tableAlarm->setItem(row,2,acknowlegedItem);
    }

    return true;
}

bool MainWindow::acknowledgeAlarm(qint64 alarmId)
{
    QSqlQuery query;

    QString sql = "UPDATE alarms SET acknowledged = 1 WHERE id = :id";

    if(!query.prepare(sql))
    {
        appendLog("准备确认写入失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    query.bindValue(":id",alarmId);

    if(!query.exec())
    {
        appendLog("确认报警失败" + query.lastError().text(),LogLevel::Error);
        return false;
    }

    if(query.numRowsAffected() != 1)
    {
        appendLog("确认报警失败：受影响记录数异常",LogLevel::Error);
        return false;
    }

    return true;
}

MainWindow::~MainWindow()
{
    delete ui;
}
