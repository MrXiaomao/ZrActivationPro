#include "detsettingwindow.h"
#include "ui_detsettingwindow.h"
#include "qcomboboxdelegate.h"
#include "globalsettings.h"
#include <QStandardItemModel>

DetSettingWindow::DetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DetSettingWindow)
{
    ui->setupUi(this);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(1, 200);
    ui->tableWidget->setRowHeight(0, 30);

    // 创建模型和视图
    // QStandardItemModel model(24, 2);
    // for (int i = 0; i < model.rowCount(); ++i) {
    //     model.setData(model.index(i, 0), QString("谱仪#%1").arg(i));
    //     model.setData(model.index(i, 1), "0.0.0.0:8000");
    // }
    // ui->tableWidget->setModel(&model);

    QComboBoxDelegate *delegate = new QComboBoxDelegate(this);
    ui->tableWidget->setItemDelegateForColumn(1, delegate); // 将委托应用到特定列
    connect(this, &DetSettingWindow::connectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        delegate->insertItem(QString("%1:%2").arg(peerAddress).arg(peerPort));
        //delegate->insertItem(peerAddress);
        //static_cast<QStandardItemModel*>(((QComboBox*)ui->tableWidget->cellWidget(0, 0))->view()->model())->item(0)->setTextAlignment(Qt::AlignCenter);
    });

    connect(this, &DetSettingWindow::disconnectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        delegate->removeItem(QString("%1:%2").arg(peerAddress).arg(peerPort));
        //delegate->removeItem(peerAddress);
    });

    connect(ui->tableWidget, &QTableWidget::currentItemChanged, this, [=](QTableWidgetItem *current, QTableWidgetItem *previous){
        if (previous)
            saveAt(previous->row() + 1);
        if (current)
            loadAt(current->row() + 1);
    });

    connect(ui->tableWidget, &QTableWidget::cellChanged, this, [=](int row, int column){
        //方式IP关联重复
        if (column == 1){
            QString peerAddress = ui->tableWidget->item(row, 1)->text();
            for (int i = 0; i < ui->tableWidget->rowCount(); ++i){
                QString peerAddress2 = ui->tableWidget->item(i, 1)->text();
                if (peerAddress == peerAddress2 && i != row){
                    if (i < row){
                        ui->tableWidget->item(row, 1)->setText("0.0.0.0:8000");
                    }
                    else {
                        ui->tableWidget->item(i, 1)->setText("0.0.0.0:8000");
                    }
                }
            }
        }
    });

    for (int i = 1; i <= 24; ++i){
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("谱仪#%1").arg(i)));
        ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->item(row, 0)->setFlags(ui->tableWidget->item(row, 0)->flags() & (~Qt::ItemIsEditable));//禁止编辑

        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(""));
        ui->tableWidget->item(row, 1)->setTextAlignment(Qt::AlignCenter);
    }

    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter> detParameters = settings->detParameters();
    for (int i=1; i<=DET_NUM; ++i){
        DetParameter detParameter = detParameters[i];
        ui->tableWidget->item(i - 1, 1)->setText(detParameter.detIp);
    }

    ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));
}

DetSettingWindow::~DetSettingWindow()
{
    delete ui;
}

void DetSettingWindow::on_pushButton_save_clicked()
{
    saveAt(ui->tableWidget->currentRow() + 1);
}


void DetSettingWindow::on_pushButton_cancel_clicked()
{
    QMetaObject::invokeMethod(this, "settingfinished", Qt::QueuedConnection);
    this->close();
}

void DetSettingWindow::saveAt(quint8 detId)
{
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[detId];

    detParameter.pluseCheckTime = ui->spinBox_pluseCheckTime->value();

    //数据接收服务器
    //IP地址
    memset(detParameter.srvIp, 0, IP_LENGTH);
    strcpy(detParameter.srvIp, ui->lineEdit_srvIp->text().toStdString().c_str());
    //子网掩码
    memset(detParameter.srvSubnetMask, 0, IP_LENGTH);
    strcpy(detParameter.srvSubnetMask, ui->lineEdit_srvSubnetMask->text().toStdString().c_str());
    //网关
    memset(detParameter.srvGateway, 0, IP_LENGTH);
    strcpy(detParameter.srvGateway, ui->lineEdit_srvGateway->text().toStdString().c_str());

    //时间服务器
    //IP地址
    memset(detParameter.timerSrvIp, 0, IP_LENGTH);
    strcpy(detParameter.timerSrvIp, ui->lineEdit_timerSrvIp->text().toStdString().c_str());

    //基本设置
    //增益
    detParameter.gain = ui->comboBox_gain->currentIndex() + 1;
    //死时间
    detParameter.deathTime = ui->spinBox_deathTime->value();
    //触发阈值
    detParameter.triggerThold = ui->spinBox_triggerThold->value();

    // //网络设置
    //IP地址
    QAbstractItemModel *model = ui->tableWidget->model();
    memset(detParameter.detIp, 0, IP_LENGTH);
    QString detIp = model->data(model->index(detId - 1, 1)).toString();
    strcpy(detParameter.detIp, detIp.toStdString().c_str());
    //MAC地址
    // char detMacAddress[MAC_LENGTH];

    //能谱设置
    //能谱刷新时间（毫秒）
    detParameter.spectrumRefreshTime = ui->spinBox_spectrumRefreshTime->value();
    //能谱长度
    detParameter.spectrumLength = ui->comboBox_spectrumLength->currentText().toUInt();

    //波形设置
    //触发模式
    detParameter.waveformTriggerMode = ui->comboBox_waveformTriggerMode->currentIndex();
    //波形长度
    detParameter.waveformLength = ui->comboBox_waveformLength->currentText().toUInt();

    //梯形成型
    //是否启用
    detParameter.trapShapeEnable = ui->trapShapeEnable->isChecked();
    //时间常数D1
    detParameter.trapShapeTimeConstD1 = ui->spinBox_trapShapeTimeConstD1->value();
    //时间常数D2
    detParameter.trapShapeTimeConstD2 = ui->spinBox_trapShapeTimeConstD2->value();
    //上升沿
    detParameter.trapShapeRisePoint = ui->spinBox_trapShapeRisePoint->value();
    //平顶
    detParameter.trapShapePeakPoint = ui->spinBox_trapShapePeakPoint->value();
    //下降沿
    detParameter.trapShapeFallPoint = ui->spinBox_trapShapeFallPoint->value();

    //高压电源
    //是否启用
    detParameter.highVoltageEnable = ui->highVoltageEnable->isChecked();
    //DAC高压输出电平
    detParameter.highVoltageOutLevel = ui->spinBox_highVoltageOutLevel->value();

    settings->sync();
}

void DetSettingWindow::loadAt(quint8 detId)
{
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[detId];

    //心跳指令间隔
    ui->spinBox_pluseCheckTime->setValue(detParameter.pluseCheckTime);

    //数据接收服务器
    //IP地址
    ui->lineEdit_srvIp->setText(QString::fromStdString(detParameter.srvIp));
    //子网掩码
    ui->lineEdit_srvSubnetMask->setText(QString::fromStdString(detParameter.srvSubnetMask));
    //网关
    ui->lineEdit_srvGateway->setText(QString::fromStdString(detParameter.srvGateway));

    //时间服务器
    //IP地址
    ui->lineEdit_timerSrvIp->setText(QString::fromStdString(detParameter.timerSrvIp));

    //基本设置
    //增益
    ui->comboBox_gain->setCurrentIndex(detParameter.gain - 1);
    //死时间
    ui->spinBox_deathTime->setValue(detParameter.deathTime);
    //触发阈值
    ui->spinBox_triggerThold->setValue(detParameter.triggerThold);

    //网络设置
    //IP地址
    //MAC地址

    //能谱设置
    //能谱刷新时间（毫秒）
    ui->spinBox_spectrumRefreshTime->setValue(detParameter.spectrumRefreshTime);
    //能谱长度
    ui->comboBox_spectrumLength->setCurrentText(QString::number(detParameter.spectrumLength));

    //波形设置
    //触发模式
    ui->comboBox_waveformTriggerMode->setCurrentIndex(detParameter.waveformTriggerMode);
    //波形长度
    ui->comboBox_waveformLength->setCurrentText(QString::number(detParameter.waveformLength));

    //梯形成型
    //是否启用
    ui->trapShapeEnable->setChecked(detParameter.trapShapeEnable);
    //时间常数D1
    ui->spinBox_trapShapeTimeConstD1->setValue(detParameter.trapShapeTimeConstD1);
    //时间常数D2
    ui->spinBox_trapShapeTimeConstD2->setValue(detParameter.trapShapeTimeConstD2);
    //上升沿
    ui->spinBox_trapShapeRisePoint->setValue(detParameter.trapShapeRisePoint);
    //平顶
    ui->spinBox_trapShapePeakPoint->setValue(detParameter.trapShapePeakPoint);
    //下降沿
    ui->spinBox_trapShapeFallPoint->setValue(detParameter.trapShapeFallPoint);

    //高压电源
    //是否启用
    ui->highVoltageEnable->setChecked(detParameter.highVoltageEnable);
    //DAC高压输出电平
    ui->spinBox_highVoltageOutLevel->setValue(detParameter.highVoltageOutLevel);
}
