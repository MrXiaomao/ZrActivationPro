#ifndef COMMHELPER_H
#define COMMHELPER_H

#include <QObject>
#include <QTcpSocket>
#include <QMutex>
#include <QFile>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QTimer>
#include <QEventLoop>
#include "TcpAgentServer.h"
#include "dataprocessor.h"
#include "qhuaweiswitcherhelper.h"

class CommHelper : public QObject
{
    Q_OBJECT
public:
    explicit CommHelper(QObject *parent = nullptr);
    ~CommHelper();

    static CommHelper *instance() {
        static CommHelper commHelper;
        return &commHelper;
    }

    /*
     打开服务
    */
    bool startServer();
    /*
     关闭服务
    */
    void stopServer();

    /**
     * 根据谱仪编号找到对应的交换机
     */
    QHuaWeiSwitcherHelper *indexOfHuaWeiSwitcher(int index);
    /**
     * 根据谱仪编号找到对应的交换机POE端口号
     */
    quint8 indexOfPort(int index);

    void connectSwitcher();

    //退出华为交换机登录
    void disconnectSwitcher();

    /*
     打开电源
    */
    void openPower();
    /*
     断开电源
    */
    void closePower();

    /*
     开始测量
    */
    void startMeasure(CommandAdapter::WorkMode mode, quint8 index = 0);
    /*
     停止测量
    */
    void stopMeasure(quint8 index = 0);

    /*
     设置发次信息
    */
    void setShotInformation(const QString shotDir, const QString shotNum);

    /*
     解析历史文件
    */
    bool openHistoryWaveFile(const QString &filePath);

    /*
     数据另存为
    */
    bool saveAs(QString dstPath);

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端上线

    Q_SIGNAL void switcherConnected(QString);//交换机连接
    Q_SIGNAL void switcherDisconnected(QString);//交换机断开

    Q_SIGNAL void detectorOnline(quint8 index);  //数采板
    Q_SIGNAL void detectorOffline(quint8 index);

    Q_SIGNAL void measureStart(quint8 index); //测量开始
    Q_SIGNAL void measureStop(quint8 index); //测量停止

    Q_SIGNAL void settingfinished();//配置完成

    Q_SIGNAL void reportDetectorTemperature(quint8, float temperature); //探测器温度报告
    Q_SIGNAL void reportTemperatureTimeout(quint8 index); //探测器温度超时报警
    Q_SIGNAL void reportSpectrumCurveData(quint8, QVector<quint32>& data);
    Q_SIGNAL void reportWaveformCurveData(quint8, QVector<quint16>& data);
    Q_SIGNAL void reportParticleCurveData(quint8, QVector<quint32>& data);

    Q_SIGNAL void reportPoePowerStatus(quint8, bool); //POE电源开关

    void exportEnergyPlot(const QString fileDir, const QString triggerTime);

    /*********************************************************
     交换机指令
    ***********************************************************/
    /*
     打开交换机POE口输出电源
    */
    bool openSwitcherPOEPower(quint8 port = 0);

    /*
     关闭交换机POE口输出电源
    */
    bool closeSwitcherPOEPower(quint8 port = 0);

    // 主动关闭POE供电，也就是温度监测被关闭的通道
    void manualCloseSwitcherPOEPower(quint8 index)
    {
        //如果该通道已经关闭，则不重复关闭
        if (mManualClosedPOEIDs.contains(index)){
            return;
        }
        mManualClosedPOEIDs.append(index);  
    }

    // 主动打开POE供电，也就是温度监测被打开的通道
    void manualOpenSwitcherPOEPower(quint8 port=0)
    {
        //如果port为0，则表示全部打开
        if (port == 0){
            mManualClosedPOEIDs.clear();
            return;
        }

        if (mManualClosedPOEIDs.contains(port))
        {
            mManualClosedPOEIDs.removeOne(port);    
        }
    }

private:
    // 处理探测器断开连接的统一逻辑
    void handleDetectorDisconnection(quint8 index);

    TcpAgentServer *mTcpServer = nullptr;//本地服务器
    QMutex mPeersMutex;
    QVector<QTcpSocket*> mConnectionPeers; //客户端连接表

    quint8 mHuaWeiSwitcherCount = 0;
    QList<QHuaWeiSwitcherHelper *> mHuaWeiSwitcherHelper;
    QString mShotDir;// 保存路径
    QString mShotNum;// 测量发次

    QMutex mMutexTriggerTimer;
    QString mTriggerTimer;//触发时钟

    QMap<quint8, DataProcessor*> mDetectorDataProcessor;//24路探测器数据处理器
    QMap<quint8, QFile*> mDetectorFileProcessor;//24路探测器数据处理器
    QMap<quint8, QVector<quint16>> mWaveAllData;
    QString mResMatrixFileName;

    //记录手动关闭POE供电的探测器ID
    QVector<quint8> mManualClosedPOEIDs;

    /*
     初始化网络
    */
    void initSocket();
    void initDataProcessor();

    /*
     分配数据处理器
    */
    void allocDataProcessor(QTcpSocket *socket);
    void freeDataProcessor(QTcpSocket *socket);

    /*
     根据IP和端口号，解析探测器编号
    */
    qint8 indexOfAddress(QString, quint16);
};

#endif // COMMHELPER_H
