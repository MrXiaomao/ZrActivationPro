#include "particalwindow.h"
#include "ui_particalwindow.h"
#include <QToolButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>

ParticalWindow::ParticalWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ParticalWindow)
    , measuring(false)
{
    ui->setupUi(this);

    commHelper = CommHelper::instance();

    //波形路径
    {
        QAction *action = ui->lineEdit_path->addAction(QIcon(":/resource/open.png"), QLineEdit::TrailingPosition);
        QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
        button->setCursor(QCursor(Qt::PointingHandCursor));
        connect(button, &QToolButton::pressed, this, [=](){
            QString dir = QFileDialog::getExistingDirectory(this);
            ui->lineEdit_path->setText(dir);
        });
    }

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=](){
        QDateTime now = QDateTime::currentDateTime();
        qint64 tt = timerStart.secsTo(now);
        QTime time = QTime(0,0,0).addSecs(tt);
        ui->label_5->setText(time.toString("HH:mm:ss"));
    });

    ui->pushButton_save->setEnabled(false);
    ui->pushButton_start->setEnabled(commHelper->isOpen());
}

ParticalWindow::~ParticalWindow()
{
    disconnect(commHelper, nullptr, this, nullptr);
    if (measuring){
        commHelper->stopMeasure();
    }

    delete ui;
}

void ParticalWindow::on_pushButton_start_clicked()
{
    if (!commHelper->isOpen()){
        QMessageBox::information(this, tr("提示"), tr("服务未开启！"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    commHelper->startMeasure(CommandAdapter::WorkMode::wmParticle);

    ui->pushButton_save->setEnabled(false);
    ui->pushButton_start->setEnabled(false);
    ui->pushButton_stop->setEnabled(true);

    QTimer::singleShot(3000, this, [=](){
        //指定时间未收到开始测量指令，则按钮恢复初始状态
        if (!measuring){
            ui->pushButton_start->setEnabled(true);
        }
    });
}


void ParticalWindow::on_pushButton_save_clicked()
{
    //存储路径
    //存储文件名
    QString filename = ui->lineEdit_path->text() + "/" + ui->lineEdit_filename->text();
    if (!filename.endsWith(".dat"))
        filename += ".dat";
    QFileInfo fInfo(filename);
    if (QFileInfo::exists(filename)){
        if (QMessageBox::question(this, tr("提示"), tr("保存文件名已经存在，是否覆盖重写？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
            return ;
    }

    // 保存参数
    // JsonSettings* waveSettings = GlobalSettings::instance()->mUserSettings;
    // if (waveSettings->isOpen()){
    //     waveSettings->prepare();
    //     waveSettings->beginGroup();
    //     QJsonObject jsonObj;
    //     waveSettings->setValue("Path", ui->lineEdit_path->text());
    //     waveSettings->setValue("FileName", filename);
    //     waveSettings->endGroup();
    //     waveSettings->flush();
    //     waveSettings->finish();

    //     commandhelper->exportFile(filename);
    // } else {
    //     QMessageBox::warning(this, tr("提示"), tr("数据保存失败！"), QMessageBox::Ok, QMessageBox::Ok);
    // }
}

