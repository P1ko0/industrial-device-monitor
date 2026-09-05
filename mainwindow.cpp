#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
