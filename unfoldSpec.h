#ifndef UNFOLDSPEC_H
#define UNFOLDSPEC_H

#include <QVector>

const int detNum = 11; //探测器通道
const int energyPoint = 500; //反解函数中能谱的道数
const int multiChannel = 512; //采样通道
class UnfoldSpec
{
public:
    UnfoldSpec();
    ~UnfoldSpec();

    QVector<QPair<double, double>> pulseSum(QMap<quint8, QVector<quint16>> data);
    void setResFileName(QString name){
        responceFileName = name;
    }
    bool loadSeq();

private:
    bool readResMatrix(QString fileName);
    double responce_matrix[detNum][energyPoint];
    QString responceFileName;
    double sampleTime = 1e-8; //两个采样点的间隔时间，单位s
    double spec[2][energyPoint];
    double seq_energy[energyPoint];
};

#endif // UNFOLDSPEC_H
