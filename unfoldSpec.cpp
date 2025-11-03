#include "unfoldSpec.h"
#include <QMap>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QVector>
#include "math.h"

UnfoldSpec::UnfoldSpec() {
    // 可以在这里初始化数组
    for (int i = 0; i < detNum; ++i) {
        for (int j = 0; j < energyPoint; ++j) {
            responce_matrix[i][j] = 0.0;
        }
    }
    for (int j = 0; j < energyPoint; ++j) {
        seq_energy[j] = 0.0;
    }
}

UnfoldSpec::~UnfoldSpec() {}

// 滑动平均滤波函数
void smooth(double *data, double *output, int data_size, int window_size) {
    // 如果窗口大小大于数据长度，则不进行滤波
    if (window_size > data_size) {
        printf("窗口大小不能大于数据大小。\n");
        return;
    }

    // 窗口宽度必须是奇数
    if ( window_size%2 == 0) {
        printf("smooth,窗口宽度只能是奇数，当前窗口宽度%d。\n", window_size);
        return;
    }

    int halfWindow = (window_size - 1) / 2;

    // 计算每个点的滑动平均值
    for (int i = 0; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;

        // 计算窗口内的平均值
        for (int j = i - (window_size - 1) / 2; j <= i + (window_size - 1) / 2; j++) {
            // 确保索引在有效范围内
            if (j >= 0 && j < data_size) {
                sum += data[j];
                count++;
            }
        }

        // 计算并存储当前位置的滑动平均值
        output[i] = sum / count;
    }
    
    // 重新处理左边界情况
    for (int i = 0; i < halfWindow; i++) {
        double sum = 0.0;
        int count = 0;
        //给出左侧端点个数
        int leftPoint = i;
        for (int j = 0; j <= i + i; j++) {
            // printf("Left side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }

    // 重新处理右边界情况
    for (int i = data_size - halfWindow; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;
        //给出右侧端点个数
        int leftPoint = data_size - i- 1;
        for (int j = i - leftPoint; j < data_size; j++) {
            // printf("Right side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }
}

/**
 * @brief readResMatrix
 * 读取相应矩阵
 */
bool UnfoldSpec::readResMatrix(QString fileName)
{
    fileName = "./responce_matrix.csv";
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开文件:" << fileName;
        return false;
    }

    QTextStream in(&file);
    // 可选：设置编码
    in.setCodec("UTF-8");

    int lineNumber = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();

        // 跳过空行和注释行
        if (line.trimmed().isEmpty() || line.startsWith('#')) {
            continue;
        }

        // 分割CSV行（支持逗号或分号分隔）
        QStringList parts = line.split(',', Qt::SkipEmptyParts);
        if (parts.size() < energyPoint) {
            parts = line.split(';', Qt::SkipEmptyParts);
        }

        if (parts.size() < energyPoint) {
            qDebug() << "第" << lineNumber << "行数据列数不足，跳过";
            continue;
        }

        // 转换为double
        bool ok;
        for(int i=0; i<energyPoint; i++){
            double value = parts[i].trimmed().toDouble(&ok);
            if(ok){
                responce_matrix[lineNumber][i] = value;
            }
            else{
                qDebug() << "第" << lineNumber << "行数据转换失败:" << line;
                file.close();
                return false;
            }
        }

        lineNumber++;
    }

    file.close();
    return true;
}

bool UnfoldSpec::loadSeq()
{
    QFile file("./seq_energy.csv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    int count = 0;
    QTextStream stream(&file);
    while (!stream.atEnd() && count<energyPoint)
    {
        QString line = stream.readLine();
        double value = line.toDouble();

        seq_energy[count] = value;
        // qDebug()<<"count = "<<count<<", "<<value<<", seq_energy[i]="<<seq_energy[count];
        count++;
    }

    file.close();
    return true;
}

/**
 * @brief UnfoldSpec::pulseSum
 * @param data 11通道的脉冲数据，11*512
 */
QVector<QPair<double, double>> UnfoldSpec::pulseSum(QMap<quint8, QVector<quint16>> data)
{
    QVector<QPair<double, double>> result;
    readResMatrix(responceFileName);

    loadSeq();

    for (int i = 0; i < detNum; ++i) {
        for (int j = 0; j < energyPoint; ++j) {
            if(responce_matrix[i][j]<1.0e-9)
                responce_matrix[i][j] = 1.0e-9;
        }
    }

    // 限制响应矩阵的最小值
    double Threshold = 1e-3;
    double phi_0[energyPoint] = {1.0};

    double data_new[detNum][multiChannel];
    double pulseMean[detNum]; //各通道脉冲幅度均值
    for (int i = 0; i < detNum; ++i) {
        QVector<quint16> chData = data[i+1];

        //求基线均值，取前n个点求平均
        double total = 0.0;
        int baseNum = 1;
        for (int j = 0; j < multiChannel; ++j) {
            data_new[i][j] = chData[j]*1.0;
            if(j<baseNum) total += chData[j]*1.0;
        }

        pulseMean[i] = total / baseNum;
    }

    //扣基线
    for (int i = 0; i < detNum; ++i) {
        for (int j = 0; j < multiChannel; ++j) {
            //qDebug()<<"data_new["<<i<<"]["<<j<<"] = "<<data_new[i][j]<<", pulseMean["<<i<<"]="<<pulseMean[i];
            data_new[i][j] = data_new[i][j] - pulseMean[i];
        }
    }

    // 求和
    double data_pulse1[detNum] = {0.0};
    for (int i = 0; i < detNum; ++i) {
        for (int j = 0; j < multiChannel; ++j) {
            data_pulse1[i] += data_new[i][j] ;
        }
        data_pulse1[i] *= sampleTime;
    }

    data_pulse1[0] = data_pulse1[0]*1E11;
    data_pulse1[1] = data_pulse1[1]*1E10;
    data_pulse1[2] = data_pulse1[2]*1E10;
    data_pulse1[3] = data_pulse1[3]*1E10;
    data_pulse1[4] = data_pulse1[4]*1E9;
    data_pulse1[5] = data_pulse1[5]*1E9;
    data_pulse1[6] = data_pulse1[6]*1E8;
    data_pulse1[7] = data_pulse1[7]*1E8;
    data_pulse1[8] = data_pulse1[8]*1E7;
    data_pulse1[9] = data_pulse1[9]*1E7;
    data_pulse1[10] = data_pulse1[10]*1E7;

    //去除负值
    double data_pulse[detNum];
    for (int i = 0; i < detNum; ++i) {
        if(data_pulse1[i]<=0.0) data_pulse[i] = 0.000001;
        else data_pulse[i] = data_pulse1[i];
    }


    double rou_old[detNum];
    for (int i = 0; i < detNum; ++i)
    {
        rou_old[i] = 1.0/sqrt(data_pulse[i]);
    }

    double log_phi_new[energyPoint] = {0.0};
    double log_phi_old[energyPoint] = {0.0};
    double N_old[detNum] = {0.0};
    for (int i = 0; i < detNum; ++i) {
        double total = 0.0;
        for (int j = 0; j < energyPoint; ++j) {
            double value = responce_matrix[i][j]*exp(log_phi_old[j]);
            total += value;
            // qDebug()<<"responce_matrix["<<i<<"]["<<j<<"] = "<<responce_matrix[i][j]
            //         <<", value ="<<value<<", total = "<<total;
            N_old[i] += responce_matrix[i][j]*exp(log_phi_old[j]);
        }
    }

    double log_N_old[detNum];
    for (int i = 0; i < detNum; ++i) {
        log_N_old[i] = log(N_old[i]);
    }

    double omega_old[detNum][energyPoint];
    for (int j = 0; j < energyPoint; ++j){
        for (int i = 0; i < detNum; ++i) {
            omega_old[i][j] = responce_matrix[i][j] * exp(log_phi_old[j])/N_old[i];
        }
    }

    double lambda_old[energyPoint] = {0.0};
    for (int j = 0; j < energyPoint; ++j){
        for (int i = 0; i < detNum; ++i) {
           lambda_old[j] += omega_old[i][j] / rou_old[i];
        }
        lambda_old[j] = 1.0 / lambda_old[j];
    }

    double Chi = 0.0;
    for (int i = 0; i < detNum; ++i)
    {
        Chi += pow(log(data_pulse[i]) - log_N_old[i], 2) / rou_old[i];
    }

    int count = 1;

    double Chi_old;
    while(1)
    {
        Chi_old = Chi;
        for (int j = 0; j < energyPoint; ++j)
        {
            double sum = 0.0;
            for (int i = 0; i < detNum; ++i)
            {
                sum += (log(data_pulse[i])-log_N_old[i]) / rou_old[i] * omega_old[i][j];
            }
            log_phi_new[j] = log_phi_old[j] + lambda_old[j] * sum;
        }
        smooth(log_phi_new, log_phi_old, energyPoint, 5);

        for (int i = 0; i < detNum; ++i)
        {
            double sum = 0.0;
            for (int j = 0; j < energyPoint; ++j)
            {
                sum += responce_matrix[i][j] * exp(log_phi_old[j]);
            }
            N_old[i] = sum;
        }

        for (int i = 0; i < detNum; ++i)
        {
            log_N_old[i] = log(N_old[i]);
        }

        for (int i = 0; i < detNum; ++i)
        {
            double sum = 0.0;
            for (int j = 0; j < energyPoint; ++j)
            {
                omega_old[i][j] = responce_matrix[i][j] * exp(log_phi_old[j])/N_old[i];
                sum += omega_old[i][j]/rou_old[i];
            }
        }

        for (int j = 0; j < energyPoint; ++j)
        {
            double sum = 0.0;
            for (int i = 0; i < detNum; ++i)
            {
                sum += omega_old[i][j]/rou_old[i];
            }
            lambda_old[j] = 1.0 / sum;
        }

        Chi = 0.0;
        for (int i = 0; i < detNum; ++i)
        {
            Chi += pow(log(data_pulse[i]) - log_N_old[i], 2)/rou_old[i];
        }

        count++;

        if ((abs(Chi-Chi_old)/Chi) < Threshold) break;
        if (count > 100)  break;
    }

    // double spec[energyPoint];
    for (int j = 0; j < energyPoint; ++j)
    {
        spec[0][j] = seq_energy[j];
        spec[1][j] = exp(log_phi_old[j]);
        //qDebug()<<" spec["<<j<<"] ="<<spec[1][j];

        result.push_back(qMakePair<double,double>(spec[0][j], spec[1][j]));
    }

    return result;
}

