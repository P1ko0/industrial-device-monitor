#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "Logger.h"
#include <QDateTime>
#include <QTableWidgetItem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
}

MainWindow::~MainWindow()
{
    delete ui;
}
