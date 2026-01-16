/*
 * @Author: MrPan
 * @Date: 2025-11-13 11:36:00
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2025-12-30 17:14:20
 * @Description: 配置文件窗口，用来配置硬件参数、网络参数等
 */
#include "localsettingwindow.h"
#include "ui_localsettingwindow.h"
#include <QHostInfo>
#include <QNetworkInterface>
#include "globalsettings.h"

#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyleOptionButton>

class CheckBoxDelegate : public QStyledItemDelegate {
protected:
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionButton checkBoxOption;
        QRect checkBoxRect = QApplication::style()->subElementRect(
            QStyle::SE_CheckBoxIndicator, &checkBoxOption, nullptr);

        // 计算居中位置
        checkBoxOption.rect = QRect(
            option.rect.center().x() - checkBoxRect.width()/2,
            option.rect.center().y() - checkBoxRect.height()/2,
            checkBoxRect.width(),
            checkBoxRect.height()
            );
        checkBoxOption.state = index.data(Qt::CheckStateRole) == Qt::Checked
                                   ? QStyle::State_On : QStyle::State_Off;

        QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkBoxOption, painter);
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                                       const QStyleOptionViewItem &option, const QModelIndex &index) {
        if (event->type() == QEvent::MouseButtonRelease) {
            // 切换勾选状态
            Qt::CheckState currentState = static_cast<Qt::CheckState>(model->data(index, Qt::CheckStateRole).toInt());
            model->setData(index, currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            return true; // 拦截事件，避免触发默认编辑
        }

        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

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

    GlobalSettings settings(CONFIG_FILENAME);
    ui->comboBox_ip->setCurrentText(settings.value("Local/ServerIp", "0.0.0.0").toString());
    ui->spinBox_port->setValue(settings.value("Local/ServerPort", 6000).toUInt());

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int column = 0; column < 24; ++column)
    {
        // 使用委托
        ui->tableWidget->setItemDelegateForColumn(column, new CheckBoxDelegate()); // 为第0列设置
        ui->tableWidget->setColumnWidth(column, 25);
        for (int row = 0; row < 3; ++row)
        {
            QTableWidgetItem* item = new QTableWidgetItem();
            item->setCheckState(Qt::CheckState::Unchecked);
            item->setTextAlignment(Qt::AlignCenter);
            ui->tableWidget->setItem(row, column, item);
        }
    }
    connect(ui->tableWidget, &QTableWidget::itemChanged, this, [=](QTableWidgetItem *item) {
        qDebug() << item->row() << item->column() << item->checkState();
        if (item->checkState() == Qt::Checked)
        {
            ui->tableWidget->blockSignals(true);

            for (int i = 0; i < ui->tableWidget->rowCount(); ++i)
            {
                if (i != item->row())
                {
                    ui->tableWidget->item(i, item->column())->setCheckState(Qt::Unchecked);
                }
                else
                {
                    ui->tableWidget->item(item->row(), item->column())->setCheckState(Qt::Checked);
                }
            }

            ui->tableWidget->blockSignals(false);
        }
        else {
            ui->tableWidget->item(item->row(), item->column())->setCheckState(Qt::Unchecked);
        }

        updateData();
    });

    // 加载配置文件
    ui->lineEdit_switcherIp_1->setText(settings.value("Switcher/1/ip", "172.18.41.94").toString());
    ui->lineEdit_switcherIp_2->setText(settings.value("Switcher/2/ip").toString());
    ui->lineEdit_switcherIp_3->setText(settings.value("Switcher/3/ip").toString());
    QRegExp regExp(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)$)");
    regExp.setPatternSyntax(QRegExp::RegExp2);
    ui->lineEdit_switcherIp_1->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_1));
    ui->lineEdit_switcherIp_2->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_2));
    ui->lineEdit_switcherIp_3->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_3));

    for (int row = 0; row < 3; ++row)
    {
        ui->tableWidget->blockSignals(true);
        QString S1 = settings.value(QString("Switcher/%1/detector").arg(row+1), "").toString();
        QStringList det1S = S1.split(",");
        for (auto& det : det1S)
        {
            int column = det.toInt();
            if (column >= 1 && column <= 24)
            {
                auto *item = ui->tableWidget->item(row, column - 1);
                if (item)
                {
                    item->setCheckState(Qt::Checked);
                }
            }
        }
        ui->tableWidget->blockSignals(false);
    }

    connect(ui->comboBox_ip, &QComboBox::currentTextChanged, this, &LocalSettingWindow::updateData);
    connect(ui->spinBox_port, &QSpinBox::textChanged, this, &LocalSettingWindow::updateData);
    connect(ui->checkBox, &QCheckBox::stateChanged, this, [=]{
        if (!ui->checkBox->isChecked()){
            ui->checkBox_2->setChecked(false);
        }
        ui->checkBox_2->setEnabled(ui->checkBox->isChecked());
    });
    connect(ui->checkBox, &QCheckBox::stateChanged, this, &LocalSettingWindow::updateData);
    connect(ui->checkBox_2, &QCheckBox::stateChanged, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_1, &QLineEdit::textChanged, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_2, &QLineEdit::textChanged, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_3, &QLineEdit::textChanged, this, &LocalSettingWindow::updateData);

    ui->checkBox->setChecked(settings.value("Switcher/count").toInt() >= 2);
    ui->checkBox_2->setChecked(settings.value("Switcher/count").toInt() >= 3);
    ui->checkBox_2->setEnabled(settings.value("Switcher/count").toInt() >= 3);
}

LocalSettingWindow::~LocalSettingWindow()
{
    delete ui;
}

void LocalSettingWindow::updateData()
{
    GlobalSettings settings(CONFIG_FILENAME);
    //本地服务配置
    settings.setValue("Local/ServerIp", ui->comboBox_ip->currentText());
    settings.setValue("Local/ServerPort", ui->spinBox_port->text());
    //交换机Telnet配置
    settings.setValue("Switcher/count", ui->checkBox->isChecked() ? (ui->checkBox_2->isChecked() ? 3 : 2) : 1);
    QCheckBox* checkBoxs[] = {nullptr, ui->checkBox, ui->checkBox_2};
    for (auto checkBox : checkBoxs)
    {
        int index = (checkBox == nullptr) ? 1 : ((checkBox == ui->checkBox) ? 2 : 3);
        QLineEdit* lineEdit = this->findChild<QLineEdit*>(QString("lineEdit_switcherIp_%1").arg(index));
        if (lineEdit)
        {
            settings.setValue(QString("Switcher/%1/ip").arg(index), lineEdit->text());
        }

        QString dets;
        for (int column = 0; column < 24; ++column)
        {
            auto *item = ui->tableWidget->item(index-1, column);
            if (item)
            {
                if (item->checkState() == Qt::Checked)
                {
                    dets += QString("%1,").arg(column+1);
                }
            }

            settings.setValue(QString("Switcher/%1/detector").arg(index), dets);
        }
    }
}

#include <QMessageBox>
void LocalSettingWindow::on_pushButton_ok_clicked()
{
    if (ui->lineEdit_switcherIp_1->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#1的IP地址不能为空！"));
        return;
    }
    if (ui->checkBox->isChecked() && ui->lineEdit_switcherIp_2->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#2的IP地址不能为空！"));
        return;
    }
    if (ui->checkBox_2->isChecked() && ui->lineEdit_switcherIp_3->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#3的IP地址不能为空！"));
        return;
    }

    updateData();
    this->close();
}


void LocalSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

