#include "parsedata.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <cstring> // 需要包含memcpy
#include "sysutils.h"

const double diffstep = 1.4901e-08;
ParseData::ParseData() {

}

ParseData::~ParseData() {}

void ParseData::mergeSpecTime_online(const FullSpectrum& specPack)
{
    // -------- 0. 先做道址压缩：8192 道 -> 2048 道，每 4 道求和 --------
    // 注意：这里直接写死 8192/4，避免和成员 G_CHANNEL 搞成运行期常量，编译更稳
    const int kCompressedCh = 8192 / 4;   // 2048
    quint32 compressedSpec[kCompressedCh];

    for (int ch = 0; ch < kCompressedCh; ++ch) {
        const int base = ch * 4;
        // 简单相加，和你后面 m_mergeSpec 的累加一样，本身就可能溢出，所以这里不额外做饱和判断
        compressedSpec[ch] =
            specPack.spectrum[base]
            + specPack.spectrum[base + 1]
            + specPack.spectrum[base + 2]
            + specPack.spectrum[base + 3];
    }

    // -------- 1. 统计计数率：用压缩后的谱 --------
    int sumCount = 0;
    //最后三道计数可能比较高，暂时不考虑它
    for (int i = 0; i < 2045; ++i) {
        sumCount += compressedSpec[i];
    }

    allSpecTime.push_back(specPack.sequence * specPack.measureTime);
    allSpecCount.push_back(sumCount);

    // 记录打靶时刻
    if (shotTime < 0 && sumCount > 1000) {
        shotTime = specPack.sequence * specPack.measureTime;
        spectDeltaT = specPack.measureTime; // 单个能谱测量时长
    }

    // 当前时刻（ms）
    qint64 currentTime = specPack.sequence * specPack.measureTime;
    int timeAfterShot = (currentTime - shotTime) / 1000; // 单位 s

    if (timeAfterShot > startTime_online) {
        quint64 lossTime = 0; // ns

        int timeMerge = timeAfterShot - startTime_online; // s
        int mergeID = int(ceil(timeMerge * 1.0 / timeBin_online));

        // 计算丢包带来的死时间
        qint64 lossTimeTemp =
            (specPack.sequence - lastSpecID_online - 1) * spectDeltaT; // ms
        lossTime = lossTimeTemp * 1000000 + specPack.deathTime * 10;   // ns

        if (m_mergeSpec.size() < mergeID) {
            // 新的分时能谱
            if (m_mergeSpec.size() > 0) {
                QVector<mergeSpecData> mergeSpec_temp = m_mergeSpec;
                // 处理前面已完整的分时能谱，进行拟合
                bool flag = getResult(mergeSpec_temp);
                Q_UNUSED(flag);
            }

            mergeSpecData tempMerge;
            tempMerge.currentTime = currentTime;
            tempMerge.deathTime   = lossTime;

            // ★ 这里用压缩后的 2048 道
            for (int i = 0; i < kCompressedCh; ++i) {
                tempMerge.spectrum[i] += compressedSpec[i];
            }
            // 后面 [kCompressedCh, 8192) 区间在构造函数里已经是 0，无需处理
            m_mergeSpec.push_back(tempMerge);

            qDebug() << "合并一个分时能谱，mergeID = " << mergeID;
        } else {
            // 往已有分时能谱里继续累加
            m_mergeSpec[mergeID - 1].currentTime = currentTime;
            m_mergeSpec[mergeID - 1].deathTime  += lossTime;

            for (int i = 0; i < kCompressedCh; ++i) {
                m_mergeSpec[mergeID - 1].spectrum[i] += compressedSpec[i];
            }
        }
    }

    lastSpecID_online = specPack.sequence;
}


bool ParseData::getResult(QVector<mergeSpecData> mergeSpec)
{
    clearFitResult();
    //根据能量刻度进行首次寻峰，进一步拟合给出511,909的峰位和半高宽
    QVector<fit_result> fit_c_2;
    {
        double* singleSpectrum = new double[G_CHANNEL];
        for(int i=0; i<G_CHANNEL; i++)
        {
            singleSpectrum[i] = mergeSpec.at(0).spectrum[i]*1.0;
        }
        double energy_scale[] = {1.272, -26.87};
        initial_PeakFind(singleSpectrum, energy_scale, fit_c_2);

        delete[] singleSpectrum;
    }

    int spec_id = 0;
    for(auto spec:mergeSpec)
    {
        qDebug()<<"specID: "<<spec_id;
        qint64 nowtime = spec.currentTime;
        double* singleSpectrum = new double[G_CHANNEL];
        for(int i=0; i<2045; i++)
        {
            singleSpectrum[i] = spec.spectrum[i]*1.0;
        }

        if(!PeakFind(singleSpectrum, fit_c_2)) return false; //注意，这里的fit_c_2每次调用后发生了更新

        //峰位漂移矫正 利用511keV 909keV做能量刻度
        double peak_calibration[2];
        peak_calibration[0] = fit_c_2[0].c1; //511
        peak_calibration[1] = fit_c_2[1].c1; //909

        // y=kx+b 线性拟合
        double k = (m_energyCalibration[0] - m_energyCalibration[1]) / (peak_calibration[0] - peak_calibration[1]);
        double b = m_energyCalibration[0] - k * peak_calibration[0];
        double new_energyScal[2] = {k, b};

        //拟合初值  fit_type = @(p, x) p(1).*exp(-1/2*((x-p(2))./p(3)).^2) + p(4).*x.^4 + p(5).*x.^3 + p(6).*x.^2 + p(7).*x + p(8);
        // QVector<double> fit_c = {fit_c_2[1].c0, 909.0, fit_c_2[1].c2*k, 1.0, 1.0, 1.0, 1.0, 1.0};
        QVector<double> fit_c = {fit_c_2[1].c0, 909.0, fit_c_2[1].c2*k, 1e-5, 1.0e-3, 1.0e-2, 0.5, 0.5};

        //能谱死时间率
        double deathRatio = spec.deathTime / 1.0e6 /spec.specTime ; //注意统一单位

        //设定剥谱能量范围
        // double energyRange[2] = {800.0, 1150.0};
        SpecStripping(singleSpectrum, new_energyScal, fit_c);
        count909_time.push_back(spec.currentTime*1.0/60000);//ms转化为min

        // 计算909keV峰面积
        // 高斯面积法
        double pi = 3.141592654;
        double peakCount = fit_c[0]*fit_c[2]/1.414*sqrt(2*pi)/(1-deathRatio);
        count909_count.push_back(peakCount);

        qDebug()<<"SpecStripping End, specID: "<<spec_id;
        spec_id++;
        delete[] singleSpectrum;
    }

    //对909全能峰计数取对数做线性拟合
    if(count909_count.size()>1)
    {
        fit909data();
        //调试用,打印909相关结果
        for(int i=0; i<count909_count.size(); i++)
        {
            qDebug() << QString("数据点[%1]: 时间=%2 min, 计数=%3, 拟合值=%4, 残差率=%5%").arg(i)
                        .arg(count909_time.at(i), 0, 'f', 3)
                        .arg(count909_count.at(i), 0, 'f', 2)
                        .arg(count909_fitcount.at(i), 0, 'f', 2)
                        .arg(count909_residual.at(i), 0, 'f', 3);
        }
        qDebug() << "================================================";
    }

    return true;
}

#include "alglibinternal.h"
#include "ap.h"
#include "interpolation.h"
#include "linalg.h"
#include "optimization.h"
#include <math.h>
using namespace alglib;
/**
 * @brief function_cx_1_func 定义待拟合函数
 * @param c
 * @param x
 * @param func
 * @param ptr 用于自定义传参
 */
void function_cx_1_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    // double peak = 511.0;
    double peak = *static_cast<double*>(ptr);
    func = c[0]*exp(-0.5*pow((x[0]-peak)/c[1],2)) + c[2]*x[0] + c[3];
    // qDebug()<<"peak="<<peak<<" c[0]="<<c[0]<<" c[1]="<<c[1]<<" c[2]="<<c[2]<<" c[3]="<<c[3]<<" x[0]="<<x[0];
}


/**
 * @brief function_cx_2_func 定义待拟合函数 func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
 * @param c
 * @param x
 * @param func
 * @param ptr
 */
void function_cx_2_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
}

/**
 * @brief function_cx_3_func 定义待拟合函数 func = c0*exp(-0.5*pow((x[0]-c1)/c2, 2)) + c3*pow(x[0], 4) + c4*pow(x[0], 3) + c5*pow(x[0], 2) + c6*x[0] + c7;
 * @param c
 * @param x
 * @param func
 * @param ptr
 */
void function_cx_3_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2], 2)) + c[3]*pow(x[0], 4) + c[4]*pow(x[0], 3) + c[5]*pow(x[0], 2) + c[6]*x[0] + c[7];
}

/**
 * @brief function_cx_4_func 定义待拟合函数 func = c0-log(2)/(78.4*60)*t;  %半衰期：4704.6min,78.4h
 * @param c
 * @param t 时间，单位min
 * @param func
 * @param ptr
 */
void function_cx_4_func(const real_1d_array &c, const real_1d_array &t, double &func, void *ptr)
{
    func = c[0] - log(2)/(78.4*60)*t[0];
}

/**
 * @brief lsqcurvefit1 候选峰拟合，假定peak为高斯部分的峰位，以此为条件进行拟合
 * func = c[0]*exp(-0.5*pow((x-peak)/c[1],2)) + c[2]*x + c[3];
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
 * @param peak  //拟合函数中的高斯部分中心道址
 * @return
 */
bool lsqcurvefit1(QVector<double> fit_x, QVector<double> fit_y, double* fit_c, double peak, double* chi_square)
{
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = fit_x.size();
    try
    {
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(4, fit_c);

        double epsx = 0.000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_1_func, NULL, &peak); //peak对应function_cx_1_func中void *ptr。
        lsfitresults(state, c, rep); //参数存储到state中

        //取出拟合参数c
        for(int i=0;i<4;i++){
            fit_c[i] = c[i];
        }

        //计算卡方统计量        
        for(int i=0; i<num; i++)
        {
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;
            double y;
            function_cx_1_func(c, xData, y, &peak);

            double residual = fit_y.at(i) - y; //残差
            (*chi_square) += pow(residual, 2)/y;
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
 * @brief lsqcurvefit2 对区域范围进行拟合 func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值。拟合成功后，会将拟合结果存放在fit_c中
 * @return 拟合是否成功，成功为1，失败为0
 */
bool lsqcurvefit2(QVector<double> fit_x, QVector<double> fit_y, double* fit_c)
{
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = fit_x.size();
    int paraNum = 5; //待拟合参数个数
    try
    {
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(paraNum, fit_c);

        double epsx = 0.000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //所有的参数数据都存储到state中的。
        lsfitreport rep;

        lsfitcreatef(x, y, c, diffstep, state); //参数存储到state中
        alglib::lsfitsetcond(state, epsx, maxits); //参数存储到state中
        alglib::lsfitfit(state, function_cx_2_func); //参数存储到state中
        lsfitresults(state, c, rep); //参数存储到state中
        // printf("lsqcurvefit2 c:%s\n", c.tostring(1).c_str());

        //取出拟合参数c
        for(int i=0; i<paraNum; i++){
            fit_c[i] = c[i];
        }

        //计算卡方统计量
        double chi_square_sum = 0.0;
        for(int i=0; i<num; i++)
        {
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;

            //计算拟合得到的y值
            double y;
            function_cx_2_func(c, xData, y, NULL);

            double residual_tmp = fit_y.at(i) - y; //残差
            double chi_square = pow(residual_tmp, 2)/y;
            chi_square_sum += chi_square;
        }
        qDebug()<<"lsqcurvefit2 c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
                 <<", r2="<<rep.r2
                 <<", terminationtype="<<rep.terminationtype
                 <<", chi_square="<<chi_square_sum;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}


/**
 * @brief lsqcurvefit3 候选峰拟合，假定peak为高斯部分的峰位，以此为条件进行拟合
 * func = c0*exp(-0.5*pow((x[0]-c1)/c2, 2)) + c3*pow(x[0], 4) + c4*pow(x[0], 3) + c5*pow(x[0], 2) + c6*x[0] + c7;
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值，拟合成功后，会将拟合结果存放在fit_c中
 * @return
 */
bool lsqcurvefit3(QVector<double> fit_x, QVector<double> fit_y, double* fit_c, QVector<double> &residual_rate)
{
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = fit_x.size();
    int paraNum = 8; //待拟合参数个数

    try
    {
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(paraNum, fit_c);

        // double epsx = 0.000001;
        double epsx = 0.00000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep;

        double diffstep_tmp = 1e-9;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep_tmp, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_3_func);
        lsfitresults(state, c, rep); //参数存储到state中

        //取出拟合参数c
        for(int i=0;i<paraNum;i++){
            fit_c[i] = c[i];
        }

        //计算残差率
        double chi_square_sum = 0.0;
        for(int i=0; i<num; i++)
        {
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;

            //计算拟合得到的y值
            double y;
            function_cx_3_func(c, xData, y, NULL);

            double residual = fit_y.at(i) - y; //残差
            // double chi_square = residual * residual / y;
            // chi_square_sum += chi_square;
            double resRate = residual / y*100.0;
            residual_rate.push_back(resRate);
        }

        qDebug().noquote()<<"lsqcurvefit3 c:["<<fit_c[0]<<","<<fit_c[1]<<","<<fit_c[2]<<","<<QString::number(fit_c[3], 'g', 9)
                 <<","<<QString::number(fit_c[4], 'g', 9)<<","<<QString::number(fit_c[5], 'g', 9)<<","<<QString::number(fit_c[6], 'g', 9)<<","<<fit_c[7]
                 <<"], iterationscount="<<rep.iterationscount
                 <<", r2="<<rep.r2
                 <<", terminationtype="<<rep.terminationtype
                 <<", chi_square="<<chi_square_sum;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}

/**
 * @brief lsqcurvefit4 对909全能峰计数随时间变化曲线进行拟合
 * func = c0 - log(2)/(78.4*60)*t;
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值，拟合成功后，会将拟合结果存放在fit_c中
 * @param residual_rate //相对残差 = 拟合残差/y*100% 
 * @return
 */
bool lsqcurvefit4(QVector<double> fit_x, QVector<double> fit_y, double* fit_c, QVector<double> &residual_rate)
{
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = fit_x.size();
    int paraNum = 1; //待拟合参数个数

    try
    {
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(paraNum, fit_c);

        // double epsx = 0.000001;
        double epsx = 0.00000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep;

        double diffstep_tmp = 1e-9;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep_tmp, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_4_func);
        lsfitresults(state, c, rep); //参数存储到state中

        //取出拟合参数c
        for(int i=0; i<paraNum; i++){
            fit_c[i] = c[i];
        }

        //计算残差率
        for(int i=0; i<num; i++)
        {
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;

            //计算拟合得到的y值
            double y;
            function_cx_4_func(c, xData, y, NULL);

            double residual = fit_y.at(i) - y; //残差
            double resRate = residual / y*100.0;
            residual_rate.push_back(resRate);
        }

        qDebug().noquote()<<"lsqcurvefit4 c:["<<fit_c[0]<<","
                 <<"], iterationscount="<<rep.iterationscount
                 <<", r2="<<rep.r2
                 <<", terminationtype="<<rep.terminationtype;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}

/**
 * @brief ParseData::initial_PeakFind 寻峰，在能量刻度的前提下，对511keV 909keV两个峰进行寻找，修正峰飘问题，给出准确的峰位。
 * @param spectrum 能谱数组，起始道址设定为1，数组长固定2045
 * @param energy_scale 能量刻度系数y=ax+b。energy_scale = {a，b};
 * @param fit_c_2 拟合参数初值，拟合成功后，更新拟合参数
 * @return bool 寻峰是否成功
 */
bool ParseData::initial_PeakFind(double* spectrum, double energy_scale[], QVector<fit_result>& fit_c_2)
{
    //平滑两次能谱曲线
    double* spectrum_smooth1 = new double[G_CHANNEL];
    double* spectrum_smooth2 = new double[G_CHANNEL];
    SysUtils::smooth(spectrum, spectrum_smooth1, G_CHANNEL, 5);
    SysUtils::smooth(spectrum_smooth1, spectrum_smooth2, G_CHANNEL, 5);

    //搜索峰的能量，keV
    double m_energyCalibration[2] = {511.0, 909.0};

    //峰位的sigma估值，该值根据能量分辨率可以初步估算得到。
    double sigma[2] = {8.0, 12.0};

    //------------- 寻找候选峰-------------------
    // QVector<fit_result> fit_c_2; //两个候选峰峰位各自一组的最优拟合参数
    for(int e = 0; e<2; e++)
    {
        //峰位搜索宽度，根据初值，给出511keV能峰的大致范围。
        quint32 leftCH, rightCH;
        double init_peak = (m_energyCalibration[e] - energy_scale[1]) / energy_scale[0];
        leftCH = floor(init_peak) - floor(2.0 * sigma[e]);
        rightCH = floor(init_peak) + floor(2.0 * sigma[e]);

        //先对峰位附近划定不同的道址范围进行拟合，每次拟合的高斯项不同
        QVector<fit_result> fit_result_All; //拟合结果汇总
        int numPeak = rightCH - leftCH + 1; //待拟合的数目

        for(int peak = leftCH; peak<=rightCH; peak++)
        {
            //确定拟合道址范围
            int start_ch = peak - 3*floor(sigma[e]);
            int end_ch = peak + 3*floor(sigma[e]);

            //提取拟合数据
            QVector<double> fitx,fity;
            fitx.reserve(end_ch - start_ch+1);
            fity.reserve(end_ch - start_ch+1);

            double maxY = 0.0;
            //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
            for(int i = start_ch-1; i<end_ch; i++)
            {
                fitx.push_back((i+1)*1.0);
                fity.push_back(spectrum_smooth2[i]*1.0);
                maxY = (maxY > spectrum_smooth2[i]) ? maxY:spectrum_smooth2[i];
            }

            //拟合参数的初值
            double p[] = {maxY, sigma[e], 10.0, 10.0};
            double chi_square = 0.0;

            //拟合并给出结果
            lsqcurvefit1(fitx, fity, p, peak*1.0, &chi_square);

            //提取拟合结果并汇总
            fit_result tempResult;
            tempResult.c0 = p[0];
            tempResult.c1 = peak*1.0;
            tempResult.c2 = p[1];
            tempResult.c3 = p[2];
            tempResult.c4 = p[3];
            tempResult.chi_square = chi_square;
            fit_result_All.push_back(tempResult);
        }

        //根据拟合结果的卡方值来确定最优高斯峰位
        //1、剔除不符合物理规律的拟合结果。
        QVector<fit_result> valid_result;
        QVector<double> valid_chi_square;
        for(auto tempResult:fit_result_All)
        {
            if(tempResult.c0 >0 &&
                tempResult.c2 <= 1.5*sigma[e] &&
                tempResult.c2 >= 0.5*sigma[e])
            {
                valid_result.push_back(tempResult);
                valid_chi_square.push_back(tempResult.chi_square);
            }
        }

        //全部拟合结果都不满足要求，则退出
        if(valid_result.size()<1) {
            qDebug()<< QString("Failed to found peak. Found peak for the first merge Spectrum, peak psition:%1").arg(m_energyCalibration[e]);
            delete[] spectrum_smooth1;
            delete[] spectrum_smooth2;
            return false;
        }

        //2、取出卡方值最小组
        int minIndex = -1;
        // 获取最小值的迭代器
        auto minIt = std::min_element(valid_chi_square.begin(), valid_chi_square.end());
        if (minIt != valid_chi_square.end()) {
            // double minValue = *minIt;                    // 最小值
            minIndex = std::distance(valid_chi_square.begin(), minIt); // 下标
            fit_result temp_result;
            temp_result = valid_result.at(minIndex);
            fit_c_2.push_back(temp_result);

            qDebug()<< QString("Sucessful to found peak for the first merge Spectrum, peak position:%1, fit results, c:").arg(m_energyCalibration[e])
                     <<temp_result.c0<<", "<<temp_result.c1<<", "<<temp_result.c2<<", "<<temp_result.c3<<", "<<temp_result.c4;
        }
    }

    //-------------候选峰二次拟合--------------
    for(int e = 0; e<2; e++)
    {
        fit_result temp_result = fit_c_2.at(e);
        int start_ch = temp_result.c1 - 2*temp_result.c2; //峰位-2sigma
        int end_ch = temp_result.c1 + 2*temp_result.c2; //峰位+2sigma

        //提取拟合数据
        QVector<double> fitx,fity;
        fitx.reserve(end_ch - start_ch+1);
        fity.reserve(end_ch - start_ch+1);

        double maxY = 0.0;
        //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
        for(int i = start_ch-1; i<end_ch; i++)
        {
            fitx.push_back((i+1)*1.0);
            fity.push_back(spectrum_smooth2[i]*1.0);
        }

        //选用上次的拟合结果作为拟合初值
        double p[5] = {temp_result.c0, temp_result.c1, temp_result.c2, temp_result.c3, temp_result.c4};

        //拟合并给出结果
        lsqcurvefit2(fitx, fity, p);

        //更新拟合结果
        fit_c_2[e].c0 = p[0];
        fit_c_2[e].c1 = p[1];
        fit_c_2[e].c2 = p[2];
        fit_c_2[e].c3 = p[3];
        fit_c_2[e].c4 = p[4];
    }

    delete[] spectrum_smooth1;
    delete[] spectrum_smooth2;
    return true;
}

/**
 * @brief ParseData::PeakFind
 * @param double* spectrum 能谱数组，起始道址设定为1，数组长固定2045
 * @param init_c 来自于上次initial_PeakFind拟合的结果。fit_type = c0*exp(-0.5*pow((x-c1)/c2,2)) + c3*x + c4;
 * @return bool 寻峰是否成功
 */
bool ParseData::PeakFind(double* spectrum, QVector<fit_result>& init_c)
{
    //平滑滤波两次能谱曲线
    double* spectrum_smooth1 = new double[G_CHANNEL];
    double* spectrum_smooth2 = new double[G_CHANNEL];
    SysUtils::smooth(spectrum, spectrum_smooth1, G_CHANNEL, 5);
    SysUtils::smooth(spectrum_smooth1, spectrum_smooth2, G_CHANNEL, 5);

    //峰位的sigma估值，该值根据能量分辨率可以初步估算得到。
    double Width = 15.0;
    //------------- 寻找候选峰-------------------
    QVector<fit_result> fit_c_2; //两个候选峰峰位各自一组的最优拟合参数
    for(int e = 0; e<2; e++)
    {
        double sigma = init_c.at(e).c2;
        //峰位搜索宽度，根据初值，给出511keV能峰的大致范围。
        quint32 leftCH, rightCH;
        double ch_peak = init_c.at(e).c1;
        leftCH = floor(ch_peak - Width);
        rightCH = floor(ch_peak + Width);

        //先对峰位附近划定不同的道址范围进行拟合，每次拟合的高斯项不同
        QVector<fit_result> fit_result_All; //拟合参数c汇总
        QVector<double> chi_square_All; //拟合结果的卡方统计量汇总
        int numPeak = rightCH - leftCH + 1; //待拟合的数目

        for(int peak = leftCH; peak<=rightCH; peak++)
        {
            //确定拟合道址范围
            int start_ch = peak - 2*floor(sigma);
            int end_ch = peak + 2*floor(sigma);

            //提取拟合数据
            QVector<double> fitx,fity;
            fitx.reserve(end_ch - start_ch+1);
            fity.reserve(end_ch - start_ch+1);

            double maxY = 0.0;
            //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
            for(int i = start_ch-1; i<end_ch; i++)
            {
                fitx.push_back((i+1)*1.0);
                fity.push_back(spectrum_smooth2[i]*1.0);
                maxY = (maxY > spectrum_smooth2[i]) ? maxY:spectrum_smooth2[i];
            }

            //拟合参数的初值
            double p[] = {maxY, sigma, 10.0, 10.0};
            double chi_square = 0.0;

            //拟合并给出结果
            lsqcurvefit1(fitx, fity, p, peak*1.0, &chi_square);

            //提取拟合结果并汇总
            fit_result temp_result;
            temp_result.c0 = p[0];
            temp_result.c1 = peak*1.0;
            temp_result.c2 = p[1];
            temp_result.c3 = p[2];
            temp_result.c4 = p[3];
            temp_result.chi_square = chi_square;
            fit_result_All.push_back(temp_result);
            chi_square_All.push_back(chi_square);
        }

        //根据拟合结果的卡方值来确定最优高斯峰位
        //1、剔除不符合物理规律的拟合结果。
        QVector<fit_result> valid_result;
        QVector<double> valid_chi_square;
        for(auto temp_result:fit_result_All)
        {
            if(temp_result.c0 >0 &&
                temp_result.c2 <= 1.5*sigma &&
                temp_result.c2 >= 0.5*sigma)
            {
                valid_result.push_back(temp_result);
                valid_chi_square.push_back(temp_result.chi_square);
            }
        }

        //全部拟合结果都不满足要求，则退出
        if(valid_result.size()<1) {
            qDebug()<< QString("Peak position%1, Failed to found peak.").arg(m_energyCalibration[e]);
            delete[] spectrum_smooth1;
            delete[] spectrum_smooth2;
            return false;
        }

        //2、取出卡方值最小组
        int minIndex = -1;
        // 获取最小值的迭代器
        auto minIt = std::min_element(valid_chi_square.begin(), valid_chi_square.end());
        if (minIt != valid_chi_square.end()) {
            // double minValue = *minIt;                    // 最小值
            minIndex = std::distance(valid_chi_square.begin(), minIt); // 下标
            fit_result temp_result;
            temp_result = valid_result.at(minIndex);
            fit_c_2.push_back(temp_result);

            qDebug()<< QString("Peak position%1, Sucessfulll found peak, fit results c:").arg(m_energyCalibration[e])
                     <<temp_result.c0<<", "<<temp_result.c1<<", "<<temp_result.c2<<", "<<temp_result.c3<<", "<<temp_result.c4;
        }
    }

    //-------------候选峰二次拟合--------------
    for(int e = 0; e<2; e++)
    {
        fit_result temp_result = fit_c_2.at(e);
        int start_ch = temp_result.c1 - 2*temp_result.c2; //峰位-2sigma
        int end_ch = temp_result.c1 + 2*temp_result.c2; //峰位+2sigma

        //提取拟合数据
        QVector<double> fitx,fity;
        fitx.reserve(end_ch - start_ch+1);
        fity.reserve(end_ch - start_ch+1);

        double maxY = 0.0;
        //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
        for(int i = start_ch-1; i<end_ch; i++)
        {
            fitx.push_back((i+1)*1.0);
            fity.push_back(spectrum_smooth2[i]*1.0);
        }

        //选用上次的拟合结果作为拟合初值
        double p[5] = {temp_result.c0, temp_result.c1, temp_result.c2, temp_result.c3, temp_result.c4};

        //拟合并给出结果
        lsqcurvefit2(fitx, fity, p);

        //更新拟合结果
        fit_c_2[e].c0 = p[0];
        fit_c_2[e].c1 = p[1];
        fit_c_2[e].c2 = p[2];
        fit_c_2[e].c3 = p[3];
        fit_c_2[e].c4 = p[4];
    }

    //返回拟合结果
    init_c = fit_c_2;

    delete[] spectrum_smooth1;
    delete[] spectrum_smooth2;
    return true;
}


/**
 * @brief ParseData::SpecStripping 剥谱
 * @param spectrum 能谱数组，起始道址设定为1，数组长固定2045
 * @param energy_scale，能量刻度系数, E = ax+b, energy_scale =[a,b]
 * @param fit_c 拟合系数,8个参数 {p0,p1,p2,p3,p4,p5,p6,p7} fit_type = p(0).*exp(-1/2*((x-p(1))./p(2)).^2) + p(3).*x.^4 + p(4).*x.^3 + p(5).*x.^2 + p(6).*x + p(7);
 * 拟合成功后，fit_c更新为拟合结果
 */
bool ParseData::SpecStripping(double* spectrum, double energy_scale[], QVector<double>& init_c)
{
    int ch_count; //待分析的能谱道数
    int leftCH, rightCH;
    leftCH = int(floor((energyRange[0] - energy_scale[1]) / energy_scale[0]));
    rightCH = int(floor((energyRange[1] - energy_scale[1]) / energy_scale[0]));
    ch_count = rightCH - leftCH;

    //提取拟合数据
    QVector<double> fitx, fity;
    fitx.reserve(ch_count);
    fity.reserve(ch_count);

    //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
    for(int i = leftCH; i<rightCH; i++)
    {
        //将道址转化为能量（keV）
        double en = (i+1) * energy_scale[0] + energy_scale[1];
        fitx.push_back(en);
        fity.push_back(spectrum[i]*1.0);
    }

    //拟合并给出结果
    QVector<double> residual_rate;
    //选用上次的拟合结果作为拟合初值
    double p[8];
    for(int i=0; i<8; i++)
    {
        p[i] = init_c[i];
    }

    lsqcurvefit3(fitx, fity, p, residual_rate);

    //提取拟合结果
    for(int i=0; i<8; i++)
    {
        init_c[i] = p[i];
    }

    //计算拟合曲线y值、残差
    alglib::real_1d_array c;
    c.setcontent(8, p);
    QVector<double> fity_curve;
    for(int i=0; i<ch_count; i++)
    {
        double x_temp = fitx.at(i);
        alglib::real_1d_array xData;
        xData.setlength(1);
        xData[0] = x_temp;
        double y;
        function_cx_3_func(c, xData, y, NULL);
        fity_curve.push_back(y);
        double residual = fity.at(i) - y; //残差
        residual = residual/y*100;
    }
    specStripData_x.append(fitx);
    specStripData_y.append(fity);
    specStripData_fity.append(fity_curve);
    specStripData_residualRate.append(residual_rate);

    //记录下标终点
    int endPos = specStripData_x.size() -1;
    if(specStrip_rightCH.size()==0) specStrip_rightCH.push_back(-1);
    specStrip_rightCH.push_back(endPos);

    return true;
}

/**
 * @brief fit909data 对909全能峰计数随时间的变化拟合出来
 * @return
 */
bool ParseData::fit909data()
{
    count909_fitcount.clear();
    count909_residual.clear();
    int ch_count = count909_count.size();
    //提取拟合数据
    QVector<double> fitx = count909_time; //浅拷贝，共用一块内存
    QVector<double> fity;

    for(int i=0; i<ch_count; i++)
    {
        fity.push_back(log(count909_count.at(i)));
    }

    //赋初值，并拟合
    double p = 10.0;
    QVector<double> residual_rate;
    lsqcurvefit4(fitx, fity, &p, residual_rate);

    //计算拟合曲线y值、残差
    alglib::real_1d_array c;
    c.setcontent(1, &p);
    QVector<double> fity_curve;
    for(int i=0; i<ch_count; i++)
    {
        double x_temp = fitx.at(i);
        alglib::real_1d_array xData;
        xData.setlength(1);
        xData[0] = x_temp;
        double y;
        function_cx_4_func(c, xData, y, NULL);
        fity_curve.push_back(exp(y));
    }

    count909_fitcount.append(fity_curve);
    count909_residual.append(residual_rate);

    return true;
}

//获取剥谱图像数据,给出第i个能谱的剥谱数据，三条曲线数据
QVector<specStripData> ParseData::GetStripData(int specID)
{
    QVector<specStripData> pictureData;
    if(specID >= specStrip_rightCH.size()-1)
    {
        printf("ERROR in \"GetStripData(int specID)\": specID is over than size of \"QVector<double> specStrip_leftCH\"");
        return pictureData;
    }

    specStripData tempData;
    int left = specStrip_rightCH.at(specID) + 1;
    int right = specStrip_rightCH.at(specID+1);
    for(int i = left; i<=right; i++)
    {
        tempData.x = specStripData_x.at(i);
        tempData.y = specStripData_y.at(i);
        tempData.fit_y = specStripData_fity.at(i);
        tempData.residual_rate = specStripData_residualRate.at(i);
        pictureData.push_back(tempData);
    }
    return pictureData;
}

QVector<specStripData> ParseData::GetCount909Data()
{
    QVector<specStripData> pictureData;
    specStripData tempData;
    for(int i=0; i<count909_time.size(); i++)
    {
        tempData.x = count909_time.at(i);
        tempData.y = count909_count.at(i);
        tempData.fit_y = count909_fitcount.at(i);
        tempData.residual_rate = count909_residual.at(i);
        pictureData.push_back(tempData);
    }
    return pictureData;
}

/**
 * @brief clearFitResult 清空拟合结果
 */
void ParseData::clearFitResult()
{
    count909_time.clear();
    count909_count.clear(); //909keV计数，采用峰面积求得
    count909_fitcount.clear(); //909keV计数随时间变化的拟合曲线
    count909_residual.clear(); //残差

    specStripData_x.clear(); //拟合数据点x坐标
    specStripData_y.clear(); //拟合数据点y坐标
    specStripData_fity.clear(); //拟合曲线y
    specStripData_residualRate.clear(); //残差
}
