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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

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
                appendLog("Modbus：未连接");
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
            appendLog("网络错误：" + socket->errorString());

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

    connect(
        ui->startButton,
        &QPushButton::clicked,
        this,
        [this]
        {
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
        );

        connect(
            ui->stopButton,
            &QPushButton::clicked,
            this,
            [this]
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
            );

    connect(
        ui->resetButton,
        &QPushButton::clicked,
        this,
        [this]
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
        );

        connect(
            ui->faultButton,
            &QPushButton::clicked,
            this,
            [this]
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

            ui->tableAlarm->item(row, 2)->setText("已确认");

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

void MainWindow::appendLog(const QString& message)
{
    QString time = QDateTime::currentDateTime()
    .toString("yyyy-MM-dd hh:mm:ss");

    ui->logEdit->appendPlainText(time + "  " + message);

    Logger::write(message.toStdString());
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

    ui->tableDevice->setItem(
        0, 2,
        new QTableWidgetItem(
            QString::number(simulator.getTemperature())
            )
        );

    ui->tableDevice->setItem(
        0, 3,
        new QTableWidgetItem(
            QString::number(simulator.getSpeed())
            )
        );
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

            int row = ui->tableAlarm->rowCount();
            ui->tableAlarm->insertRow(row);

            ui->tableAlarm->setItem(
                row, 0, new QTableWidgetItem(time)
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

MainWindow::~MainWindow()
{
    delete ui;
}
