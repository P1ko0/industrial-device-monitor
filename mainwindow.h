#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QString>
#include "Device.h"
#include "Simulator.h"

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
    Device device;
    Simulator simulator;
    bool saveParameters();
    bool loadParameters();
    void appendLog(const QString& message);
};
#endif // MAINWINDOW_H
