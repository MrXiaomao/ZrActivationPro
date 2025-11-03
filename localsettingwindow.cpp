#include "localsettingwindow.h"
#include "ui_localsettingwindow.h"
#include <QHostInfo>
#include <QNetworkInterface>
#include "globalsettings.h"

LocalSettingWindow::LocalSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LocalSettingWindow)
{
    ui->setupUi(this);

    //获取本机IP地址列表
    //获取主机名称
    QString HostName = QHostInfo::localHostName();
    //根据主机名获取主机IP地址
    QHostInfo hostinfo = QHostInfo::fromName(HostName);
    QList<QHostAddress> AddressList = hostinfo.addresses();
    if(!AddressList.isEmpty())
    {
        for(int i = 0;i<AddressList.size();i++)
        {
            QHostAddress hostaddress = AddressList.at(i);
            if(hostaddress.protocol() == QAbstractSocket::IPv4Protocol)
            {
                ui->comboBox_ip->addItem(hostaddress.toString());
            }
        }
    }

    ui->comboBox_ip->addItem("127.0.0.1");
    ui->comboBox_ip->addItem("0.0.0.0");

    GlobalSettings settings;
    ui->comboBox_ip->setCurrentText(settings.value("Local/ServerIp", "0.0.0.0").toString());
    ui->spinBox_port->setValue(settings.value("Local/ServerPort", 6000).toUInt());
    ui->lineEdit_switcherIp->setText(settings.value("Switcher/Telnet/Ip", "192.168.1.253").toString());
}

LocalSettingWindow::~LocalSettingWindow()
{
    delete ui;
}

void LocalSettingWindow::on_pushButton_ok_clicked()
{
    GlobalSettings settings;
    settings.setValue("Local/ServerIp", ui->comboBox_ip->currentText());
    settings.setValue("Local/ServerPort", ui->spinBox_port->text());
    settings.setValue("Switcher/Telnet/Ip", ui->lineEdit_switcherIp->text());
    settings.setValue("Switcher/Telnet/Port", 23);

    this->close();
}


void LocalSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

