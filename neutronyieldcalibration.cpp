#include "neutronyieldcalibration.h"
#include "ui_neutronyieldcalibration.h"
#include <QSettings>

NeutronYieldCalibration::NeutronYieldCalibration(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NeutronYieldCalibration)
{
    ui->setupUi(this);
    ui->pushButton_import->hide();
    ui->pushButton_export->hide();

    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<ui->tableWidget->rowCount(); ++row){
        QTableWidgetItem *item = new QTableWidgetItem(mSettings.value(QString("Table/yield%1").arg(row+1), "0.0").toString());
        item->setTextAlignment(Qt::AlignCenter); // 水平+垂直居中
        ui->tableWidget->setItem(row, 0, item);

        QTableWidgetItem *item2 = new QTableWidgetItem(mSettings.value(QString("Table/activity%1").arg(row+1), "0.0").toString());
        item2->setTextAlignment(Qt::AlignCenter); // 水平+垂直居中
        ui->tableWidget->setItem(row, 1, item2);
    }
    ui->tableWidget->setItemDelegate(new NumberDelegate(this));
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

NeutronYieldCalibration::~NeutronYieldCalibration()
{
    delete ui;
}

void NeutronYieldCalibration::on_pushButton_import_clicked()
{

}


void NeutronYieldCalibration::on_pushButton_export_clicked()
{

}

#include <QMessageBox>
void NeutronYieldCalibration::on_pushButton_save_clicked()
{
    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<ui->tableWidget->rowCount(); ++row){
        mSettings.setValue(QString("Table/yield%1").arg(row+1), ui->tableWidget->item(row, 0)->text());
        mSettings.setValue(QString("Table/activity%1").arg(row+1), ui->tableWidget->item(row, 1)->text());
    }

    QMessageBox::information(this, tr("提示"), "数据已保存。");
}


void NeutronYieldCalibration::on_pushButton_cancel_clicked()
{
    this->close();
}

QVector<QPair<double, double>> NeutronYieldCalibration::neutronYield()
{
    QVector<QPair<double, double>> result;
    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<24; ++row){
        double yield = mSettings.value(QString("Table/yield%1").arg(row+1), "0.0").toDouble();
        double activity = mSettings.value(QString("Table/activity%1").arg(row+1), "0.0").toDouble();
        result.push_back(qMakePair(yield, activity));
    }

    return result;
}
