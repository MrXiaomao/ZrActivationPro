#include "netsettingwindow.h"
#include "ui_netsettingwindow.h"

NetSettingWindow::NetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NetSettingWindow)
{
    ui->setupUi(this);
}

NetSettingWindow::~NetSettingWindow()
{
    delete ui;
}

void NetSettingWindow::on_pushButton_save_clicked()
{
    this->close();
}


void NetSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

