#include "mainwindow.h"
#include "./ui_mainwindow.h"

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
            device.reset();

            ui->statusLabel->setText(
                QString::fromStdString(device.getStatus())
                );
        }
    );
}

MainWindow::~MainWindow()
{
    delete ui;
}
