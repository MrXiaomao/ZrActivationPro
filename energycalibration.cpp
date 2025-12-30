/*
 * @Author: MrPan
 * @Date: 2025-12-29 22:08:10
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2025-12-30 16:55:42
 * @Description: 用于能量刻度，可进行线性拟合、二次函数拟合，能量刻度点的添加、删除、拟合等功能
 */
#include "energycalibration.h"
#include "ui_energycalibration.h"
#include "qcustomplot.h"

EnergyCalibration::EnergyCalibration(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::EnergyCalibration),
    saveStatus(false),
    datafit(false),
    fitType(0)
{
    ui->setupUi(this);

    initCustomPlot();

    // 保存界面参数
    QSettings mSettings("./config/Calibration.ini", QSettings::IniFormat);
    int count = mSettings.value("Table/count", 0).toInt();
    for (int i= 0; i<count; ++i){
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(mSettings.value(QString("Table/channel%1").arg(i+1)).toString())));
        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString("%1").arg(mSettings.value(QString("Table/energy%1").arg(i+1)).toString())));
    }

    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

EnergyCalibration::~EnergyCalibration()
{
    delete ui;
}

void EnergyCalibration::initCustomPlot()
{
    // 创建绘图区域
    customPlot = new QCustomPlot(ui->widget_plot);
    customPlot->setObjectName("customPlot");
    customPlot->setLocale(QLocale(QLocale::Chinese, QLocale::China));
    ui->widget_plot->layout()->addWidget(customPlot);

    // 设置选择容忍度，即鼠标点击点到数据点的距离
    //customPlot->setSelectionTolerance(5);
    // 设置全局抗锯齿
    customPlot->setAntialiasedElements(QCP::aeAll);
    //customPlot->setNotAntialiasedElements(QCP::aeAll);
    // 图例名称隐藏
    customPlot->legend->setVisible(false);
    // 图例名称显示位置
    customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignRight);
    // 设置边界
    customPlot->setContentsMargins(0, 0, 0, 0);
    // 设置标签倾斜角度，避免显示不下
    customPlot->xAxis->setTickLabelRotation(-45);
    // 背景色
    customPlot->setBackground(QBrush(Qt::white));
    // 图像画布边界
    customPlot->axisRect()->setMinimumMargins(QMargins(0, 0, 0, 0));
    // 坐标背景色
    customPlot->axisRect()->setBackground(Qt::white);
    // 允许拖拽，缩放
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // 允许轴自适应大小
    //customPlot->graph(0)->rescaleValueAxis(true);
//    customPlot->xAxis->rescale(true);
//    customPlot->yAxis->rescale(true);

    // 设置刻度范围
//    QSharedPointer<QCPAxisTickerFixed> axisTickerFixed(new QCPAxisTickerFixed);
//    axisTickerFixed->setTickStep(256);
//    axisTickerFixed->setScaleStrategy(QCPAxisTickerFixed::ssNone);
//    customPlot->xAxis->setTicker(axisTickerFixed);
//    customPlot->xAxis->setRange(0, 2048);
//    customPlot->yAxis->setRange(0, 100);
//    customPlot->yAxis->ticker()->setTickCount(5);
//    customPlot->xAxis->ticker()->setTickCount(8);
    // 设置刻度可见
    customPlot->xAxis->setTicks(true);
    customPlot->xAxis2->setTicks(false);
    customPlot->yAxis->setTicks(true);
    customPlot->yAxis2->setTicks(false);
    // 设置轴线可见
    customPlot->xAxis->setVisible(true);
    customPlot->xAxis2->setVisible(true);
    customPlot->yAxis->setVisible(true);
    customPlot->yAxis2->setVisible(true);
    //customPlot->axisRect()->setupFullAxesBox();//四边安装轴并显示
    // 设置刻度标签可见
    customPlot->xAxis->setTickLabels(true);
    customPlot->xAxis2->setTickLabels(false);
    customPlot->yAxis->setTickLabels(true);
    customPlot->yAxis2->setTickLabels(false);
    // 设置子刻度可见
    customPlot->xAxis->setSubTicks(true);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis->setSubTicks(true);
    customPlot->yAxis2->setSubTicks(false);
    //设置轴标签名称
    customPlot->xAxis->setLabel(QObject::tr("道址"));
    customPlot->yAxis->setLabel(QObject::tr("能量/keV"));
    // 设置网格线颜色
    customPlot->xAxis->grid()->setPen(QPen(QColor(180, 180, 180, 128), 1, Qt::PenStyle::DashLine));
    customPlot->yAxis->grid()->setPen(QPen(QColor(180, 180, 180, 128), 1, Qt::PenStyle::DashLine));
    customPlot->xAxis->grid()->setSubGridPen(QPen(QColor(50, 50, 50, 128), 1, Qt::DotLine));
    customPlot->yAxis->grid()->setSubGridPen(QPen(QColor(50, 50, 50, 128), 1, Qt::DotLine));
    customPlot->xAxis->grid()->setZeroLinePen(QPen(QColor(50, 50, 50, 100), 1, Qt::SolidLine));
    customPlot->yAxis->grid()->setZeroLinePen(QPen(QColor(50, 50, 50, 100), 1, Qt::SolidLine));
    // 设置网格线是否可见
    customPlot->xAxis->grid()->setVisible(false);
    customPlot->yAxis->grid()->setVisible(false);
    // 设置子网格线是否可见
    customPlot->xAxis->grid()->setSubGridVisible(false);
    customPlot->yAxis->grid()->setSubGridVisible(false);

    // 文本元素随窗口变动而变动
    fixedTextTtem = new QCPItemText(customPlot);
    fixedTextTtem->setColor(Qt::black);
    fixedTextTtem->position->setType(QCPItemPosition::ptAbsolute);
    fixedTextTtem->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    fixedTextTtem->setTextAlignment(Qt::AlignLeft);
    fixedTextTtem->setFont(QFont(font().family(), 12));
    fixedTextTtem->setPadding(QMargins(8, 0, 0, 0));
    fixedTextTtem->position->setCoords(20.0, 20.0);
    fixedTextTtem->setText("");

    // 添加散点图
    QCPGraph *curGraphDot = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
    curGraphDot->setPen(QPen(Qt::black));
    curGraphDot->setLineStyle(QCPGraph::lsNone);// 取消线性图，改为散点图
    curGraphDot->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));

    QCPGraph *curGraphCurve = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
    curGraphCurve->setPen(QPen(Qt::blue, 2, Qt::PenStyle::SolidLine));
    curGraphCurve->setLineStyle(QCPGraph::lsLine);

    connect(customPlot, &QCustomPlot::afterLayout, this, [=](){
        fixedTextTtem->position->setCoords(customPlot->axisRect()->topLeft().x(), customPlot->axisRect()->topLeft().y());
    });
    // 图形刷新
    customPlot->replot();
}

void EnergyCalibration::on_pushButton_add_clicked()
{
    // 添加
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);
    ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(ui->doubleSpinBox_x->value())));
    ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString("%1").arg(ui->doubleSpinBox_y->value())));
}

void EnergyCalibration::on_pushButton_del_clicked()
{
    // 删除
    QList<QTableWidgetItem*> selItems = ui->tableWidget->selectedItems();
    if (selItems.count() <= 0)
        return;

    QTableWidgetItem* selItem = selItems[0];
    int row = selItem->row();
    ui->tableWidget->removeRow(row);
}

void EnergyCalibration::on_pushButton_fit_clicked()
{
    points.clear();
    datafit = false;

    for (int i= 0; i<ui->tableWidget->rowCount(); ++i){
        double x, y;
        x = ui->tableWidget->item(i, 0)->text().toDouble();
        y = ui->tableWidget->item(i, 1)->text().toDouble();
        points.push_back(QPointF(x, y));
    }

    // 检查数据点数量
    if(points.size() < 2)
    {
        ui->textEdit_log->append("拟合失败：数据点不足，至少需要2个数据点才能进行拟合\n\n");
        return;
    }

    if(ui->btnFit1->isChecked())
    {
        //第一个拟合函数
        calculate(1);
    }
    else if(ui->btnFit2->isChecked())
    {
        //第二个拟合函数
        if(points.size() < 3)
        {
            ui->textEdit_log->append("拟合失败：二次函数拟合至少需要3个数据点\n\n");
            return;
        }
        calculate(2);
    }

    /*
    // 保存界面参数
    QSettings mSettings("./config/Calibration.ini", QSettings::IniFormat);
    mSettings.setValue("Table/count", ui->tableWidget->rowCount());
    for (int i= 0; i<ui->tableWidget->rowCount(); ++i){
        double x, y;
        mSettings.setValue(QString("Table/channel%1").arg(i+1), ui->tableWidget->item(i, 0)->text());
        mSettings.setValue(QString("Table/energy%1").arg(i+1), ui->tableWidget->item(i, 1)->text());
    }*/
}

/**
 * @brief 定义待拟合函数 f=ax+b
 * @param c 拟合参数c
 * @param x 自变量x
 * @param func 函数值
 * @param ptr 用于自定义传参
 */
void EnergyCalibration::function_cx_1_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    func = c[0]*x[0] + c[1];
}

/**
 * @brief 定义待拟合函数 f=ax^2+bx+c
 * @param c 拟合参数c
 * @param x 自变量x
 * @param func 函数值
 * @param ptr 用于自定义传参
 */
void EnergyCalibration::function_cx_2_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    func = c[0]*x[0]*x[0] + c[1]*x[0] + c[2];
}

/**
 * @brief lsqcurvefit1 线性拟合
 * func = c[0]*x + c[1];
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
 * @param R2    //拟合决定系数
 * @return
 */
bool EnergyCalibration::lsqcurvefit1(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
{
    int paraNum = 2; //待拟合参数个数
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = points.size();
    try
    {
        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }
        
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(paraNum, fit_c);

        const double diffstep = 1.4901e-08;
        double epsx = 0.000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep_local;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_1_func); //peak对应function_cx_1_func中void *ptr。
        lsfitresults(state, c, rep_local); //参数存储到state中

        //取出拟合参数c
        for(int i=0; i<paraNum; i++){
            fit_c[i] = c[i];
        }

        //计算拟合y值        
        for(int i=0; i<num; i++)
        {
            double y;
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;
            function_cx_1_func(c, xData, y, NULL);
        }
        *R2 = rep_local.r2;
        
        // 如果提供了rep指针，则复制报告信息
        if(rep != nullptr)
        {
            *rep = rep_local;
        }

        // printf("lsqcurvefit1 c:%s, chi_square=%.1f\n", c.tostring(1).c_str(), *chi_square);
        // qDebug()<<"lsqcurvefit1 c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
        //          <<", r2="<<rep.r2
        //          <<", terminationtype="<<rep.terminationtype
        //          <<"chi_square="<<*chi_square;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}

/**
 * @brief lsqcurvefit2 二次函数拟合
 * func = c[0]*x^2 + c[1]*x + c[2];
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
 * @param R2    //拟合决定系数
 * @return
 */
bool EnergyCalibration::lsqcurvefit2(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
{
    int paraNum = 3; //待拟合参数个数
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = points.size();
    try
    {
        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }
        
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(paraNum, fit_c);

        const double diffstep = 1.4901e-08;
        double epsx = 0.000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep_local;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_2_func); //peak对应function_cx_2_func中void *ptr。
        lsfitresults(state, c, rep_local); //参数存储到state中

        //取出拟合参数c
        for(int i=0; i<paraNum; i++){
            fit_c[i] = c[i];
        }

        //计算拟合y值        
        /*for(int i=0; i<num; i++)
        {
            double y;
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;
            function_cx_2_func(c, xData, y);
        }*/
        *R2 = rep_local.r2;
        
        // 如果提供了rep指针，则复制报告信息
        if(rep != nullptr)
        {
            *rep = rep_local;
        }
        
        // printf("lsqcurvefit1 c:%s, chi_square=%.1f\n", c.tostring(1).c_str(), *chi_square);
        // qDebug()<<"lsqcurvefit1 c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
        //          <<", r2="<<rep.r2
        //          <<", terminationtype="<<rep.terminationtype
        //          <<"chi_square="<<*chi_square;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}

void EnergyCalibration::calculate(int no)
{
    //线性拟合 y=ax+b
    if(no == 1)
    {
        double R2 = 0.0;
        double fit_c[2] = {1.0, 1.0};
        lsfitreport rep;
        if(lsqcurvefit1(points, fit_c, &R2, &rep))
        {
            C[0] = fit_c[0];
            C[1] = fit_c[1];
            datafit = true;
            fitType = 1;

            if (C[1] > 0)
                fixedTextTtem->setText(QString("y = %1 * x + %2 \nR² = %3").arg(C[0]).arg(C[1]).arg(R2));
            else
                fixedTextTtem->setText(QString("y = %1 * x - %2 \nR² = %3").arg(C[0]).arg(qAbs(C[1])).arg(R2));
            
            // 更新textEdit_log
            QString logText = QString("========== 拟合信息 ==========\n");
            logText += QString("拟合类型: 线性拟合 (y = ax + b)\n");
            logText += QString("拟合参数:\n");
            logText += QString("  a = %1\n").arg(C[0], 0, 'g', 10);
            logText += QString("  b = %1\n").arg(C[1], 0, 'g', 10);
            logText += QString("决定系数 R² = %1\n").arg(R2, 0, 'g', 10);
            logText += QString("迭代次数: %1\n").arg(rep.iterationscount);
            // logText += QString("终止类型: %1\n").arg(rep.terminationtype);
            logText += QString("数据点数: %1\n").arg(points.size());
            logText += QString("=============================\n\n");
            ui->textEdit_log->setText(logText);
        }
        else
        {
            ui->textEdit_log->setText("拟合失败：线性拟合计算错误\n\n");
        }
    }
    else if(no == 2) //二次函数拟合 y=ax^2+bx+c
    {
        double R2 = 0.0;
        double fit_c[3] = {1.0, 1.0, 1.0};
        lsfitreport rep;
        if(lsqcurvefit2(points, fit_c, &R2, &rep))
        {
            C[0] = fit_c[0];
            C[1] = fit_c[1];
            C[2] = fit_c[2];
            datafit = true;
            fitType = 2;

            fixedTextTtem->setText(QString("y = %1 * x² + %2 * x + %3\nR² = %4").arg(C[0]).arg(C[1]).arg(C[2]).arg(R2));
            
            // 更新textEdit_log
            QString logText = QString("========== 拟合信息 ==========\n");
            logText += QString("拟合类型: 二次函数拟合 (y = ax² + bx + c)\n");
            logText += QString("拟合参数:\n");
            logText += QString("  a = %1\n").arg(C[0], 0, 'g', 10);
            logText += QString("  b = %1\n").arg(C[1], 0, 'g', 10);
            logText += QString("  c = %1\n").arg(C[2], 0, 'g', 10);
            logText += QString("决定系数 R² = %1\n").arg(R2, 0, 'g', 10);
            logText += QString("迭代次数: %1\n").arg(rep.iterationscount);
            // logText += QString("终止类型: %1\n").arg(rep.terminationtype);
            logText += QString("数据点数: %1\n").arg(points.size());
            logText += QString("=============================\n\n");
            ui->textEdit_log->setText(logText);
        }
        else
        {
            ui->textEdit_log->setText("拟合失败：二次函数拟合计算错误\n\n");
        }
    }

    //绘制待拟合点
    {
        QVector<double> keys, values;        
        for (int i = 0; i < points.size(); i++)
        {
            double x = points[i].x(), y = points[i].y();
            keys << x;
            values << y;
        }

        customPlot->graph(0)->setData(keys, values);
    }

    //绘制拟合曲线
    double maxx = 8192.0;
    if(datafit)
    {
        QVector<double> keys, values;
        
        double nx, ny;
        for (nx = 0.0; nx <= maxx; nx += 0.1)
        {
            if(ui->btnFit1->isChecked())
            {
                // ny = C[0] + C[1] * x;
                alglib::real_1d_array c;
                c.setcontent(2, C);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = nx;
                function_cx_1_func(c, xData, ny, NULL);
            }
            else
            {
                // ny = C[0] * x *x + C[1] + C[2] * x;
                //计算y值
                alglib::real_1d_array c;
                c.setcontent(3, C);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = nx;
                function_cx_2_func(c, xData, ny, NULL);
            }

            keys << nx;
            values << ny;
        }

        customPlot->graph(1)->setData(keys, values);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    
    //X轴和Y轴两端各保留5%的空间
    double SPANX = (customPlot->xAxis->range().upper - customPlot->xAxis->range().lower) * 0.05;
    double SPANY = (customPlot->yAxis->range().upper - customPlot->yAxis->range().lower) * 0.05;
    customPlot->xAxis->setRange(customPlot->xAxis->range().lower - SPANX, customPlot->xAxis->range().upper + SPANX);
    customPlot->yAxis->setRange(customPlot->yAxis->range().lower - SPANY, customPlot->yAxis->range().upper + SPANY);
    customPlot->replot();
}

void EnergyCalibration::on_btn_clearTable_clicked()
{
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);
}

//保存参数
void EnergyCalibration::on_bt_SaveFit_clicked()
{
    if(saveStatus)
    {
        QMessageBox::information(this, "提示", "已保存过拟合结果，请勿重复保存");
        return;
    }
    saveStatus = true;
    if(fitType == 1)
    {
        mUserSettings->setValue("EnCalibrration_k", C[1]);
        mUserSettings->setValue("EnCalibrration_b", C[0]);
    }
    else
    {
        mUserSettings->setValue("EnCalibrration_a", C[0]);
        mUserSettings->setValue("EnCalibrration_b", C[1]);
        mUserSettings->setValue("EnCalibrration_c", C[2]);
    }
}

