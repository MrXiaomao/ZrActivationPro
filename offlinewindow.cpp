/*
 * @Author: MrPan
 * @Date: 2025-04-20 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2025-08-01 16:50:56
 * @Description: 离线数据分析
 */
#include "offlinewindow.h"
#include "ui_offlinewindow.h"
#include "globalsettings.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QAction>
#include <QToolButton>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <math.h>

OfflineWindow::OfflineWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::OfflineWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
{
    ui->setupUi(this);

    this->initCustomPlot();
    //this->applyColorTheme();

    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for(int i=0; i<3; i++){
        for(int j=3; j<6; j++){
            ui->tableWidget->item(j, i)->setText(QString::number(0));
        }
    }

    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}


OfflineWindow::~OfflineWindow()
{
    delete ui;
}

void OfflineWindow::initCustomPlot()
{

}

void OfflineWindow::on_pushButton_start_clicked()
{

}

void OfflineWindow::on_pushbutton_save_clicked()
{

}
