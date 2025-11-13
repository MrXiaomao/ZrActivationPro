#include "dataprocessor.h"
#include <QDebug>
#include <QTimer>

DataProcessor::DataProcessor(quint8 index, QTcpSocket* socket, QObject *parent)
    : CommandAdapter(parent)
    , mIndex(index)
{
    mDataProcessThread = new QLiteThread(this);
    mDataProcessThread->setWorkThreadProc([=](){
        while (!mTerminatedDataThread)
        {
            {
                QMutexLocker locker(&mDataLocker);
                if (mRawData.size() == 0){
                    while (!mDataReady){
                        mDataCondition.wait(&mDataLocker);
                    }

                    mCachePool.append(mRawData);
                    mRawData.clear();
                    mDataReady = false;
                }
                else{
                    mCachePool.append(mRawData);
                    mRawData.clear();
                }
            }

            if (!mTerminatedDataThread)
                analyzeCommands(mCachePool);
        }
    });
    mDataProcessThread->start();
    connect(this, &DataProcessor::destroyed, [=]() {
        mDataProcessThread->exit(0);
        mDataProcessThread->wait(500);
    });

    if (mTcpSocket)
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));
}


DataProcessor::~DataProcessor()
{
    if (mDataProcessThread){
        // 终止线程
        mTerminatedDataThread = true;
        mDataReady = true;
        mDataCondition.wakeAll();
        mDataProcessThread->wait();
    }
}

void DataProcessor::reallocIndex(quint8 index)
{
    mIndex = index;
}

void DataProcessor::reallocSocket(QTcpSocket *tcpSocket, DetParameter& detParameter)
{
    if (mTcpSocket){
        disconnect(mTcpSocket, SIGNAL(readyRead()), this, nullptr);
    }

    mTcpSocket = tcpSocket;
    if (mTcpSocket){
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));

        updateSetting(detParameter);
    }
}

void DataProcessor::updateSetting(DetParameter& detParameter)
{
    // 设置基本参数
    {
        //清空指令
        this->clear();

        //#01 增益=1|0|1|1234000FFA1000000000ABCD
        this->sendGain(false, detParameter.gain);
        //#02 死时间=1|0|3|1234000FFA1100000000ABCD
        this->sendDeathTime(false, detParameter.deathTime);
        //#03 触发阈值=1|0|5|1234000FFA1200000000ABCD
        this->sendTriggerThold(false, detParameter.triggerThold);
        //#04 触发模式=1|0|7|1234000FFC1000000003ABCD
        this->sendWaveformMode(false, (TriggerMode)detParameter.waveformTriggerMode, detParameter.waveformLength);
        //#05 能谱刷新时间=1|0|9|1234000FFD1000000000ABCD
        this->sendSprectnumRefreshTimelength(false, detParameter.spectrumRefreshTime);
        //#06 能谱长度=1|0|11|1234000FFD1100000000ABCD
        //this->sendSpectrumLength(false, detParameter.spectrumLength);
        if (detParameter.trapShapeEnable){
            //#07 梯形成型常数=1|0|13|1234000FFE1000000000ABCD
            this->sendTrapTimeConst(false, detParameter.trapShapeTimeConstD1, detParameter.trapShapeTimeConstD2);
            //#08 上升沿参数=1|0|15|1234000FFE1100000000ABCD
            this->sendRisePeakFallPoints(false, detParameter.trapShapeRisePoint, detParameter.trapShapePeakPoint, detParameter.trapShapeFallPoint);
        }
        //#09 梯形成型使能=1|0|17|1234000FFE1200000000ABCD
        this->sendTrapShapeEnable(false, (TrapShapeEnable)detParameter.trapShapeEnable);
        //#10 高压电平使能=1|0|19|1234000FF91000000000ABCD
        this->sendHighVolgateOutLevelEnable(false, (HighVolgateOutLevelEnable)detParameter.highVoltageEnable);
        //#11 高压输出电平=1|0|21|1234000FF91100000000ABCD
        if (detParameter.highVoltageEnable)
            this->sendHighVolgateOutLevel(false, detParameter.highVoltageOutLevel);

        //通知发送指令
        this->notifySendNextCmd();
    }
}

bool DataProcessor::isFreeSocket()
{
    return mTcpSocket == nullptr;
}

void DataProcessor::startMeasure(WorkMode workMode)
{
    /*开始测量之前清空所有数据，防止野数据存在*/
    mCachePool.clear();

    //#12 波形工作模式=1|0|22|1234000FFF1000000000ABCD
    //#13 能谱工作模式=1|0|23|1234000FFF1000000001ABCD
    //#14 粒子工作模式=1|0|24|1234000FFF1000000002ABCD
    this->sendWorkMode(false, workMode);

    //#15 开始测量=1|0|25|1234000FEA1000000001ABCD
    this->sendStartMeasure();
}

/*
     * 停止测量
     */
void DataProcessor::stopMeasure()
{
    //#16 停止测量=1|0|26|1234000FEA1000000000ABCD
    this->sendStopMeasure();
}

void DataProcessor::sendCmdToSocket(QByteArray& cmd) const
{
    if (mTcpSocket && mTcpSocket->isOpen()){
        mTcpSocket->write(cmd);
        mTcpSocket->waitForBytesWritten();
        ::QThread::msleep(50);

        qDebug().noquote()<< "[" << mIndex << "] "<< "Send HEX[" << cmd.size() << "]: " << cmd.toHex(' ');
    }
}

void DataProcessor::readyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket->bytesAvailable() <= 0)
        return;

    QByteArray rawData = socket->readAll();
    this->inputData(rawData);
}

void DataProcessor::inputData(const QByteArray& data)
{
    {
        QMutexLocker locker(&mDataLocker);
        if (!mIsMeasuring) //进入正式测量之前，数据量偏小，都是指令集，可以输出打印
            qDebug().noquote()<< "[" << mIndex << "] "<< "Recv HEX[" << data.size() << "]: " << data.toHex(' ');

        mRawData.append(data);
        mDataReady = true;
    }

    mDataCondition.wakeAll();
}

void DataProcessor::inputSpectrumData(quint8 no, QByteArray& data){
    mSpectrumData[no] = data;

    // 能谱数据：一个能谱数据包长度为256*32bit，但完整的能谱数据为8192*32bit。
    if (mSpectrumData.size() == 32){
        QVector<quint32> spectrum;
        for (int index=1; index<=32; ++index){
            QByteArray &chunk = mSpectrumData[index];
            bool ok = false;
            quint32 serialNumber = data.mid(6, 4).toHex().toUInt(&ok, 16);//能谱序号
            quint32 measureTime = data.mid(10, 4).toHex().toUInt(&ok, 16);//测量时间
            quint32 deathTime = data.mid(14, 4).toHex().toUInt(&ok, 16);//死时间
            quint32 serialNo = data.mid(18, 4).toHex().toUInt(&ok, 16);//能谱编号（1~32）

            for (int i=0; i<256; i+=4){

                quint32 amplitude = data.mid(22+i, 4).toHex().toUShort(&ok, 16);
                spectrum.append(amplitude);
            }
        }

        QMetaObject::invokeMethod(this, "reportSpectrumCurveData", Qt::QueuedConnection, Q_ARG(quint8, mIndex), Q_ARG(QVector<quint32>&, spectrum));
    }
};
