#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QtEndian>
#include <QTcpSocket>
#include <QFile>
#include <QDateTime>

#include "qlitethread.h"
#include "commandadapter.h"
#include "globalsettings.h"

class DataProcessor : public CommandAdapter
{
    Q_OBJECT
public:
    explicit DataProcessor(quint8 index, QTcpSocket* socket = nullptr, QObject *parent = nullptr);
    ~DataProcessor();

    virtual void sendCmdToSocket(QByteArray&) const override;

    void reallocIndex(quint8 index);//重新分配通道索引
    void reallocSocket(QTcpSocket *tcpSocket, DetParameter& detParameter);
    bool isFreeSocket();//是否关联Socket

    /*
     * 添加数据
     */
    void inputData(const QByteArray& data);

    /*
     * 开始测量
     */
    void startMeasure(WorkMode workMode);

    /*
     * 停止测量
     */
    void stopMeasure();

    quint8 index(){
        return this->mIndex;
    };

    void updateSetting(DetParameter& detParameter);

public slots:
    void readyRead();

signals:   
    void relayConnected();// 继电器
    void relayDisconnected();
    void relayPowerOn();
    void relayPowerOff();

    void detectorConnected(quint8 index);  // 探测器
    void detectorDisconnected(quint8 index);

    // void showRealCurve(const QMap<quint8, QVector<quint16>>& data);//实测曲线
    // void showEnerygySpectrumCurve(const QVector<QPair<double, double>>& data);//反解能谱

private:
    quint8 mIndex;//探测器索引
    QTcpSocket *mTcpSocket = nullptr;

    QByteArray mRawData; // 存储网络原始数据
    QByteArray mCachePool; // 缓存数据，数据处理之前，先转移到二级缓存池
    QMap<quint8, QVector<quint16>> mRealCurve;// 4路通道实测曲线数据

    bool mDataReady = false;// 数据长度不够，还没准备好
    bool mTerminatedDataThread = false;
    QMutex mDataLocker;
    QWaitCondition mDataCondition;
    QLiteThread* mDataProcessThread = nullptr;// 处理线程
    bool mWaveMeasuring = false;     //波形测量中
    quint8 mTransferMode = 0x00;//传输模式
    quint32 mWaveLength = 512;// 波形长度
    quint8 mChWaveDataValidTag = 0x00;//通道数据是否完整
};

#endif // DATAPROCESSOR_H
