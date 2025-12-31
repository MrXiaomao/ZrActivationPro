#ifndef ENERGYCALIBRATION_H
#define ENERGYCALIBRATION_H

#include <QWidget>

#include "ap.h"
#include "interpolation.h"
#include <math.h>
using namespace alglib;
namespace Ui {
class EnergyCalibration;
}

class QCustomPlot;
class QCPItemText;
class QRadioButton;
class QButtonGroup;
class EnergyCalibration : public QWidget
{
    Q_OBJECT

public:
    explicit EnergyCalibration(QWidget *parent = nullptr);
    ~EnergyCalibration();

signals:
    void doit(int x, int y);

private slots:
    void on_pushButton_add_clicked();

    void on_pushButton_del_clicked();

    void on_pushButton_fit_clicked();

    void on_btn_clearTable_clicked();

    void on_bt_SaveFit_clicked();
    
    void on_detRadioButton_toggled(bool checked);

private:
    void initCustomPlot();
    void initUI();
    
private:
    static void function_cx_1_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr);
    static void function_cx_2_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr);
    bool lsqcurvefit1(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep = nullptr);
    bool lsqcurvefit2(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep = nullptr);
    void calculate(int);

    //绘制散点与拟合曲线
    void drawScatterAndFitCurve(QVector<double> points_X, QVector<double> points_Y, int fitType);
    
    Ui::EnergyCalibration *ui;
    QCustomPlot* customPlot;
    QCPItemText *fixedTextTtem; //图例

    bool saveStatus; // 是否已经保存过拟合结果
    bool datafit; // 是否拟合成功
    QVector<QPointF> points;
    qreal C[5]; //拟合函数参数
    int fitType; //拟合函数类型,1:线性拟合,2:二次函数拟合
    QVector<QRadioButton*> detRadioButtons; // 存储所有探测器单选按钮指针
    QButtonGroup* detButtonGroup; // 管理单选按钮组，确保互斥
};

#endif // ENERGYCALIBRATION_H
