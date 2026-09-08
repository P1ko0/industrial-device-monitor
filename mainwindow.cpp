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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

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

    ui->tableWidget->setRowCount(1);

    timer = new QTimer(this);

    connect(
        timer,
        &QTimer::timeout,
        this,
        [this]
        {
            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );

            temp++;
            speed += 100;

            ui->tableWidget->setItem(
                0,
                0,
                new QTableWidgetItem("Device1")
                );

            ui->tableWidget->setItem(
                0,
                1,
                new QTableWidgetItem(
                    QString::fromStdString(device.getStatus())
                    )
                );

            ui->tableWidget->setItem(
                0,
                2,
                new QTableWidgetItem(
                    QString::number(temp)
                    )
                );

            ui->tableWidget->setItem(
                0,
                3,
                new QTableWidgetItem(
                    QString::number(speed)
                    )
                );
        }
        );

        timer->start(500);

    connect(
        ui->startButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            device.start();

            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );
        }
    );

    connect(
        ui->stopButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            device.stop();

            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );
        }
    );

    connect(
        ui->resetButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            auto oldStatus = device.getStatus();


            device.reset();

            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );

            if ((oldStatus == "stopped" || oldStatus == "error")
                && device.getStatus() == "idle")
            {
                QString time = QDateTime::currentDateTime()
                .toString("yyyy-MM-dd hh:mm:ss");

                ui->logEdit->appendPlainText(time + "  设备已复位");
                Logger::write("设备已复位");
            }
        }
    );

    connect(
        ui->faultButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            device.fault();

            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );

            int row = ui->tableAlarm->rowCount();
            ui->tableAlarm->insertRow(row);

            QString time = QDateTime::currentDateTime()
                               .toString("yyyy-MM-dd hh:mm:ss");

            ui->logEdit->appendPlainText(time + "  设备发生故障");
            Logger::write("设备发生故障");

            ui->tableAlarm->setItem(
                row, 0, new QTableWidgetItem(time)
                );

            ui->tableAlarm->setItem(
                row, 1, new QTableWidgetItem("设备发生故障")
                );

            ui->tableAlarm->setItem(
                row, 2, new QTableWidgetItem("未确认")
                );
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

            QString time = QDateTime::currentDateTime()
                               .toString("yyyy-MM-dd hh:mm:ss");

            ui->tableAlarm->item(row, 2)->setText("已确认");

            ui->logEdit->appendPlainText(time + "已确认");
            Logger::write("报警已确认");
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

MainWindow::~MainWindow()
{
    delete ui;
}
