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

    //测量前下发配置
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[mIndex];
    this->updateSetting(detParameter);

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

/**
 * @brief DataProcessor::inputSpectrumData
 * @param no 探测器通道编号
 * @param data 完整的能谱数据包，传入进来时已经验证了包的完整性
 */
void DataProcessor::inputSpectrumData(quint8 no, QByteArray& data){
    // 能谱数据：一个能谱数据包长度为256*32bit，但完整的能谱数据为8192*32bit。
    // 1. 提取数据包数据（256道 quint32 数据）
    quint32 spectrumSeq = 0;
    SubSpectrumPacket subSpecPackket;
    if (!extractSpectrumData(data, subSpecPackket)) {
        qWarning() << "Extract sub-spectrum data failed, seq:" << spectrumSeq
                   << ", DetID:" << static_cast<int>(no);
        return;
    }
    spectrumSeq = subSpecPackket.spectrumSeq;

    // 2. 查找/创建对应序号的完整能谱（线程安全）
    FullSpectrum* fullSpectrum = nullptr;
    {
        QMutexLocker locker(&mDataLocker); // 保护 mFullSpectrums 共享资源

        // 使用 find 方法检查是否存在，避免重复插入
        // 使用 find 方法检查是否存在，避免重复插入
        auto it = mFullSpectrums.find(spectrumSeq);
        if (it == mFullSpectrums.end()) {
            // 不存在，创建新能谱并插入
            FullSpectrum newSpectrum = {}; // 初始化所有成员为默认值（0/false/空）
            newSpectrum.sequence = spectrumSeq;
            newSpectrum.isComplete = false;
            // receivedPackets 默认为空集合，无需额外初始化

            it = mFullSpectrums.insert(spectrumSeq, newSpectrum);
        }
        fullSpectrum = &(it.value());
    }

    // 3. 数据拼接与状态更新（核心逻辑，线程安全）
    {
        QMutexLocker locker(&mDataLocker);
        // 跳过已接收的重复子包（避免覆盖和重复计数）
        if (fullSpectrum->receivedPackets.contains(subSpecPackket.spectrumSubNo)) {
            qDebug() << "Duplicate sub-spectrum packet, seq:" << spectrumSeq
                     << ", part:" << static_cast<int>(subSpecPackket.spectrumSubNo) << "(ignored)";
            return;
        }

        // 计算子包在完整能谱中的偏移量（256道/子包）
        const int kSubSpectrumSize = sizeof(quint32) * 256;
        const int targetOffset = (subSpecPackket.spectrumSubNo-1) * 256; // 第n个子包对应偏移 n*256 道

        // 拷贝子能谱数据到完整能谱（固定数组直接操作内存，高效安全）
        memcpy(&fullSpectrum->spectrumData[targetOffset], // 目标地址：完整能谱对应偏移
               subSpecPackket.spectrum,                   // 源数据：当前子包256道数据
               kSubSpectrumSize);                         // 拷贝字节数：256*4=1024字节

        // 标记该子包已接收
        fullSpectrum->receivedPackets.insert(subSpecPackket.spectrumSubNo);

        // 4. 检查是否所有子包都已接收（32个子包齐全则拼接完成）
        const quint8 kMinPacketNo = 1;
        const quint8 kMaxPacketNo = 32;
        if (fullSpectrum->receivedPackets.size() == kMaxPacketNo) {
            fullSpectrum->isComplete = true;
            fullSpectrum->completeTime = QDateTime::currentDateTime();

            // 5. 提取元数据（测量时间、死时间）- 从当前子包提取（确保数据完整性）
            const int kMetaDateMinLen = 18; // 测量时间(10-13) + 死时间(14-17)，需至少18字节
            fullSpectrum->measureTime = subSpecPackket.measureTime;
            fullSpectrum->deadTime = subSpecPackket.deadTime;

            // 6. 发送完整能谱信号（使用 QueuedConnection 避免线程阻塞）
            // 注意：QVector 拷贝完整数据，避免多线程访问冲突
            QVector<quint32> completeData;
            completeData.reserve(8192); // 预分配内存，提升效率
            for (int i = 0; i < 8192; ++i) {
                completeData.append(fullSpectrum->spectrumData[i]);
            }

            // 异步发送信号（确保接收方在主线程处理，避免UI阻塞）
            QMetaObject::invokeMethod(
                this,
                "reportSpectrumCurveData",
                Qt::QueuedConnection,
                Q_ARG(quint8, mIndex),          // 设备索引
                Q_ARG(QVector<quint32>, completeData) // 完整8192道数据
                );

            // 7. 清理已完成的能谱数据（释放内存，可选：若需保留历史数据可注释）
            // mFullSpectrums.remove(spectrumSeq);
            qDebug() << "Get a full spectrum, SpectrumID:" << spectrumSeq
                     << ", specMeasureTime(ms):" << fullSpectrum->measureTime
                     << ", deathTime(*10ns):" << fullSpectrum->deadTime;
        }
    }
};

/**
 * @brief 从数据包中提取能谱数据
 * @param packetData 完整的数据包
 * @param spectrum 输出的能谱数据数组
 * @return 成功返回true，失败返回false
 */
bool DataProcessor::extractSpectrumData(const QByteArray& packetData, SubSpectrumPacket& packet)
{
    // 检查数据包长度
    const int expectedSize = sizeof(SubSpectrumPacket);
    if (packetData.size() < expectedSize) {
        qWarning() << "数据包长度不足，期望:" << expectedSize << "实际:" << packetData.size();
        return false;
    }

    // 拷贝到结构体
    memcpy(&packet, packetData.constData(), sizeof(SubSpectrumPacket));

    // 处理字节序（Windows是小端序，网络数据通常是大端序）
    packet.header = qFromBigEndian<quint32>(packet.header);
    packet.dataType = qFromBigEndian<quint16>(packet.dataType);
    packet.spectrumSeq = qFromBigEndian<quint32>(packet.spectrumSeq);
    packet.measureTime = qFromBigEndian<quint32>(packet.measureTime);
    packet.deadTime = qFromBigEndian<quint32>(packet.deadTime);
    packet.spectrumSubNo = qFromBigEndian<quint16>(packet.spectrumSubNo);
    packet.timeMs = qFromBigEndian<quint32>(packet.timeMs);
    packet.tail = qFromBigEndian<quint32>(packet.tail);

    // 拷贝能谱数据并处理字节序
    for (int i = 0; i < 256; ++i) {
        packet.spectrum[i] = qFromBigEndian<quint32>(packet.spectrum[i]);
    }

    // 调试信息
    qDebug() << "Get a subSpectrum Packets, spectrumSeq:" << packet.spectrumSeq
             << "subSquenceID:" << packet.spectrumSubNo;

    return true;
}
