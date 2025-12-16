#ifndef PARSEDATA_H
#define PARSEDATA_H

//中子产额分析算法
#include <QString>
#include <QVector>
#include <QtEndian> //qFromBigEndian需要
#include <cstring>      // 用于内存初始化（如memset）
#include <QVector>
#include <QTextStream> // 添加文本流支持

#include "globalsettings.h"

// 存放拟合参数值 fit_type = c0*exp(-0.5*pow((x-c1)/c2,2)) + c3*x + c4;
struct fit_result{
    double c0; //高斯部分的峰高
    double c1; //高斯部分的峰位
    double c2; //高斯部分的sigma
    double c3;
    double c4;
    double chi_square; //拟合结果卡方值
};

struct specStripData{
    double x; //待拟合数据点x
    double y; //待拟合数据点y
    double fit_y; //拟合曲线y
    double residual_rate; //残差率
};

enum paraseMode{
    offlineMode = 0,
    onlineMode = 1,
};

class ParseData
{
public:
    // 数据包结构定义,合并后的能谱数据，已经做了丢包处理
    #pragma pack(push, 1) // 确保1字节对齐
    struct mergeSpecData {
        qint64 currentTime; //能谱测量结束时刻，打靶时刻为零时刻，早于打靶为负数，单位ms
        quint64 specTime;   //能谱测量对应时长，单位ms
        quint64 deathTime;  //能谱测量死时间，单位ns
        quint32 spectrum[8192];  // 能谱，前三道恒为0.

        // 1. 默认构造函数：初始化所有成员为默认值，确保spectrum前三道为0
        mergeSpecData()
            : currentTime(0),
            specTime(0),
            deathTime(0) {
            // 初始化spectrum数组
            for (int i = 0; i < 8192; ++i) {
                spectrum[i] = 0;
            }
        }
    };
    #pragma pack(pop) // 恢复默认对齐

public:
    ParseData();
    ~ParseData();
    
    QVector<mergeSpecData> GetMergeSpec(){ return m_mergeSpec;}

    //获取剥谱图像数据,给出第i个能谱的剥谱数据，三条曲线数据
    QVector<specStripData> GetStripData(int specID);

    QVector<specStripData> GetCount909Data();

    /**
     * @brief mergeSpecTime_online
     *
     */
    void mergeSpecTime_online(const FullSpectrum& specPack);

    // 对第一段能谱尝试寻峰
    bool initial_PeakFind(double* spectrum, double energy_scale[], QVector<fit_result>& fit_c_2);

    // 寻峰
    bool PeakFind(double* spectrum, QVector<fit_result>& init_c);

    // 剥谱
    bool SpecStripping(double* spectrum, double energy_scale[], QVector<double>& init_c);

    // 拟合909全能峰计数随时间的变化
    bool fit909data();

    void setStartTime(int time)
    {
        T0_beforeShot = time;
    }

private:
    bool getResult(QVector<mergeSpecData> mergeSpec);

    void clearFitResult();

    QVector<mergeSpecData> m_mergeSpec; //对原始数据汇总后的各时段能谱，对丢包带来的死时间做了相应记录

    QVector<int> allSpecTime; //每一个计数点对应的时刻，考虑到可能丢包，所以时刻并不是连续的。
    QVector<int> allSpecCount; //每秒能谱总计数随时间的变化

    const double m_energyCalibration[2] = {511.0, 909.0}; //用于能量刻度的特征峰
    const double energyRange[2] = {800.0, 1150.0};//设定剥谱能量范围

    const int G_CHANNEL = 2048; //能谱的道数
    qint32 T0_beforeShot = 0; //能谱开测时刻相对于打靶零时刻的时间（单位s，可正数可负数)T0_beforeShot = 开测时刻 - 打靶时刻

    // 用来存放剥谱处理结果的图像数据，结合GetStripData()
    // 所有的拟合数据依次压入specStripData数据中
    QVector<double> count909_time; //各分时能谱对应测量结束时间（也就是右端点），单位min
    QVector<double> count909_count; //909keV计数，采用峰面积求得
    QVector<double> count909_fitcount; //909keV计数随时间变化的拟合曲线
    QVector<double> count909_residual; //残差

    QVector<double> specStripData_x; //拟合数据点x坐标
    QVector<double> specStripData_y; //拟合数据点y坐标
    QVector<double> specStripData_fity;//拟合曲线y
    QVector<double> specStripData_residualRate; //残差

    paraseMode m_parasemode;
    int shotTime = -1; //记录打靶起始时间，单位ms (FPGA内部时钟，仪器开始测量时为时钟为零)，必须在大靶前开始测量，这样才能找到计数率暴增点（打靶瞬间）
    int lastSpecID_online = 0; //上一个能谱的编号。用于在线处理
    quint64 spectDeltaT = 0; //单个能谱测量时长，单位ms
    int startTime_online = 240*60; //解析能谱的起始时刻，单位s
    int timeBin_online = 300*60; //解析能谱的时间宽度，单位s.分时能谱的单个能谱测量时间

    QVector<int> specStrip_rightCH; //存放所有剥谱图形的右端点下标
};

#endif // PARSEDATA_H
