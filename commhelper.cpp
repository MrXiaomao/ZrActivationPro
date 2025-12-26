#include "commhelper.h"
#include "globalsettings.h"

#include <QTimer>
#include <QDataStream>
#include <QNetworkSession>
#include <QNetworkConfigurationManager>

CommHelper::CommHelper(QObject *parent)
    : QObject{parent}
{
    /*初始化网络*/
    initSocket();
    initDataProcessor();

    connect(this, &CommHelper::settingfinished, this, [=](){
        //更改了设置，这里需要重新对数据处理器进行关联
        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
            QTcpSocket* connection = *it;
            allocDataProcessor(connection);

            ++it;
        }
    });
}

CommHelper::~CommHelper()
{
    this->stopServer();

    for (int index = 1; index <= DET_NUM; ++index){
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (detectorDataProcessor)
            detectorDataProcessor->deleteLater();
    }
    mDetectorDataProcessor.clear();
}

void CommHelper::initSocket()
{
    this->mTcpServer = new TcpAgentServer();
    connect(this->mTcpServer, &TcpAgentServer::newConnection, this, [=](qintptr socketDescriptor){
        QMutexLocker locker(&mPeersMutex);
        QTcpSocket* connection = new QTcpSocket(this);
        connection->setSocketDescriptor(socketDescriptor);
        this->mConnectionPeers.push_back(connection);

        //网络异常（与下面disconnected信号会重复）
        // connect(connection, &QAbstractSocket::errorOccurred, this, [=](QAbstractSocket::SocketError){
        //     QString peerAddress = connection->property("peerAddress").toString();
        //     quint16 peerPort = connection->property("peerPort").toUInt();
        //     QMetaObject::invokeMethod(this, "disconnectPeerConnection", Qt::QueuedConnection, Q_ARG(QString, peerAddress), Q_ARG(quint16, peerPort));

        //     //根据配置解析是哪一路探测器下线了
        //     qint8 index = indexOfAddress(peerAddress, peerPort);
        //     if (index > 0)
        //         QMetaObject::invokeMethod(this, "detectorOffline", Qt::QueuedConnection, Q_ARG(quint8, index));

        //     auto it = this->mConnectionPeers.begin();
        //     while (it != this->mConnectionPeers.end()) {
        //         if (*it == connection) {
        //             //网络掉线了，取消数据处理器关联
        //             freeDataProcessor(connection);

        //             connection->deleteLater();
        //             it = this->mConnectionPeers.erase(it);
        //         } else {
        //             ++it;
        //         }
        //     }
        // });
        connect(connection, &QAbstractSocket::disconnected, this, [=](){
            QString peerAddress = connection->property("peerAddress").toString();
            quint16 peerPort = connection->property("peerPort").toUInt();

            //根据配置解析是哪一路探测器下线了
            qint8 index = indexOfAddress(peerAddress, peerPort);
            if (index > 0)
            {
                handleDetectorDisconnection(index);
            }
        });

        QString peerAddress = connection->peerAddress().toString();
        quint16 peerPort = connection->peerPort();
        connection->setProperty("peerAddress", peerAddress);
        connection->setProperty("peerPort", peerPort);

        //给新上线客户端分配数据处理器
        allocDataProcessor(connection);
        QMetaObject::invokeMethod(this, "connectPeerConnection", Qt::QueuedConnection, Q_ARG(QString, peerAddress), Q_ARG(quint16, peerPort));

        //根据配置解析是哪一路探测器上线了
        qint8 index = indexOfAddress(peerAddress, peerPort);
        if (index > 0)
            QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection, Q_ARG(quint8, index));
    });

    GlobalSettings settings(CONFIG_FILENAME);
    mHuaWeiSwitcherCount =  settings.value("Switcher/Count", 0).toUInt();
    mHuaWeiSwitcherHelper.reserve(mHuaWeiSwitcherCount);
    for (int i=0; i<mHuaWeiSwitcherCount; ++i){
        QString ip = settings.value(QString("Switcher/%1/ip").arg(i+1), "").toString();
        QString ass = settings.value(QString("Switcher/S%1").arg(i+1), "").toString();
        QHuaWeiSwitcherHelper* huaWeiSwitcherHelper = new QHuaWeiSwitcherHelper(ip);
        huaWeiSwitcherHelper->setAssociatedDetector(ass);

        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::switcherConnected, this, &CommHelper::switcherConnected);
        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::switcherDisconnected, this, &CommHelper::switcherDisconnected);
        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::reportPoePowerStatus, this, &CommHelper::reportPoePowerStatus);

        mHuaWeiSwitcherHelper.push_back(huaWeiSwitcherHelper);
    }
}

void CommHelper::initDataProcessor()
{
    for (int index = 1; index <= DET_NUM; ++index){
        DataProcessor* detectorDataProcessor = new DataProcessor(index, nullptr, this);
        mDetectorDataProcessor[index] = detectorDataProcessor;

        // 更新温度数据
        connect(detectorDataProcessor, &DataProcessor::reportTemperatureData,
        this, [=](double temperature){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit reportDetectorTemperature(processor->index(), temperature);
        });
        
        //对温度超时报警进行处理
        connect(detectorDataProcessor, &DataProcessor::reportTemperatureTimeout, this, [=](){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            quint8 detID = processor->index();
            emit reportTemperatureTimeout(detID);//上报温度超时报警

            handleDetectorDisconnection(detID);

            //如果该探测器在手动关闭POE供电列表中，则不处理
            if (mManualClosedPOEIDs.contains(detID)){
                return;
            }

            //断电30min后重新打开供电
            stopMeasure(detID);
            //先停止测量
            int stopDelay = 30; //min
            qInfo().noquote() << "探测器" << detID 
                    << QString("温度心跳超时报警！停止测量并断电%1min后重新打开供电!").arg(stopDelay);
            //断电
            closeSwitcherPOEPower(detID);
            //定时30min后重新打开供电
            QTimer::singleShot(stopDelay*60*1000, this, [=](){
                openSwitcherPOEPower(detID);
                qInfo().noquote() << "探测器" << detID << "重启供电";
            });
        });

        connect(detectorDataProcessor, &DataProcessor::reportSpectrumData, this, [=](QByteArray data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            /*
                保存数据
            */
            {
                QMutexLocker locker(&mMutexTriggerTimer);
                if (mTriggerTimer.isEmpty()){
                    mTriggerTimer = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
                }

                if (!mDetectorFileProcessor.contains(processor->index())){
                    QString filePath = QString("%1/%2/%3_%4.dat").arg(mShotDir).arg(mShotNum).arg(mTriggerTimer).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly);

                    qInfo().noquote().nospace() << "[谱仪#"<< processor->index() << "]创建存储文件：" << filePath;
                }

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                }
            }

            // 数据解包
            detectorDataProcessor->inputSpectrumData(processor->index(), data);
        });

        connect(detectorDataProcessor, &DataProcessor::reportSpectrumCurveData,
                this, [=](quint8 index, QVector<quint32> spectrum){
                    emit reportSpectrumCurveData(index, spectrum);
                });

        connect(detectorDataProcessor, &DataProcessor::reportWaveformData, this, [=](QByteArray data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            /*
                保存数据
            */
            quint8 detId = processor->index();
            {
                QMutexLocker locker(&mMutexTriggerTimer);
                if (mTriggerTimer.isEmpty()){
                    mTriggerTimer = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
                }

                if (!mDetectorFileProcessor.contains(processor->index())){
                    QString filePath = QString("%1/%2/%3_%4.dat").arg(mShotDir).arg(mShotNum).arg(mTriggerTimer).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly);

                    qInfo().noquote().nospace() << "[谱仪#"<< processor->index() << "]创建存储文件：" << filePath;
                }

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                    //mDetectorFileProcessor[processor->index()]->flush();
                }
            }

            data.remove(0, 6);//移除包头
            data.chop(8);//移除包尾

            //从HDF5加载配置信息
            HDF5Settings *settings = HDF5Settings::instance();
            QMap<quint8, DetParameter>& detParameters = settings->detParameters();
            DetParameter& detParameter = detParameters[detId];
            quint32 waveLen = detParameter.waveformLength;

            //暂时只取第一个波形出来
            {
                // 数据解包
                QVector<quint16> waveform;
                waveform.reserve(waveLen-2);//减去包头包尾两个数据
                for (int i=2; i<(waveLen-1)*2; i+=2){//跨过包头两个字节
                    bool ok;
                    quint16 amplitude = data.mid(i, 2).toHex().toUShort(&ok, 16);
                    waveform.append(amplitude);
                }

                emit reportWaveformCurveData(processor->index(), waveform);
            }
        });

        connect(detectorDataProcessor, &DataProcessor::reportParticleData, this, [=](QByteArray data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());            
            //emit reportParticleCurveData(processor->index(), data);
        });
    }
}

/**
 * 根据谱仪编号找到对应的交换机
 */
QHuaWeiSwitcherHelper *CommHelper::indexOfHuaWeiSwitcher(int index)
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        if (switcherHelper->contains(index))
            return switcherHelper;
    }

    return nullptr;
}

/**
 * 根据谱仪编号找到对应的交换机POE端口号
 */
quint8 CommHelper::indexOfPort(int index)
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        if (switcherHelper->contains(index))
            return switcherHelper->indexOfPort(index);
    }

    return 0x00;
}

void CommHelper::allocDataProcessor(QTcpSocket *socket)
{
    QString peerAddress = socket->peerAddress().toString();
    quint16 peerPort = socket->peerPort();
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    QString addr1 = QString("%1:%2").arg(peerAddress).arg(peerPort);

    // IP+Port全匹配
    {
        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
            for (int index = 1; index <= DET_NUM; ++index){
                DetParameter& detParameter = detParameters[index];
                QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
                if (addr1 == addr2){
                    mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                    return;
                }
            }

            ++it;
        }
    }

    // IP匹配
    {
        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
            for (int index = 1; index <= DET_NUM; ++index){
                DetParameter& detParameter = detParameters[index];
                QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
                if (addr2 == peerAddress){
                    mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                    return;
                }
            }

            ++it;
        }
    }

    //重新分配一个之前从没绑定过的数据处理器
    for (int index = 1; index <= DET_NUM; ++index){
        if (mDetectorDataProcessor[index]->isFreeSocket()){
            DetParameter& detParameter = detParameters[index];
            QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
            if (addr2 == "0.0.0.0:6000"){//0.0.0.0:6000是数据库默认初始化值
                //给新上线网络连接分配一个空闲的探测器
                qstrcpy(detParameter.det_Ip_port, addr1.toStdString().c_str());
                settings->sync();
                mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                return;
            }
        }
    }

    //重新分配空闲的数据处理器
    for (int index = 1; index <= DET_NUM; ++index){
        if (mDetectorDataProcessor[index]->isFreeSocket()){
            DetParameter& detParameter = detParameters[index];
            qstrcpy(detParameter.det_Ip_port, addr1.toStdString().c_str());
            settings->sync();
            mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
            return;
        }
    }
}

void CommHelper::freeDataProcessor(QTcpSocket *socket)
{
    QString peerAddress = socket->property("peerAddress").toString();
    quint16 peerPort = socket->property("peerPort").toUInt();
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    QString addr1 = QString("%1:%2").arg(peerAddress).arg(peerPort);

    auto it = this->mConnectionPeers.begin();
    while (it != this->mConnectionPeers.end()) {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
            if (addr2 == addr1){
                mDetectorDataProcessor[index]->reallocSocket(nullptr, detParameter);
                break;
            }
        }

        ++it;
    }
}
/*
 打开交换机POE口输出电源
*/
bool CommHelper::openSwitcherPOEPower(quint8 index)
{
    QHuaWeiSwitcherHelper *switcherHelper = indexOfHuaWeiSwitcher(index);
    if (nullptr == switcherHelper)
        return false;

    return switcherHelper->openSwitcherPOEPower(indexOfPort(index));
}

/*
 关闭交换机POE口输出电源
*/
bool CommHelper::closeSwitcherPOEPower(quint8 index)
{
    QHuaWeiSwitcherHelper *switcherHelper = indexOfHuaWeiSwitcher(index);
    if (nullptr == switcherHelper)
        return false;

    return switcherHelper->closeSwitcherPOEPower(indexOfPort(index));
}

/////////////////////////////////////////////////////////////////////////////////
/*
 打开网络
*/
bool CommHelper::startServer()
{    
    GlobalSettings settings;
    return this->mTcpServer->listen(QHostAddress(settings.value("Local/ServerIp", "0.0.0.0").toString()), settings.value("Local/ServerPort", 6000).toUInt());
}
/*
 断开网络
*/
void CommHelper::stopServer()
{
    for (auto connection : mConnectionPeers){
        connection->close();
        connection->deleteLater();
        connection = nullptr;
    }
    mConnectionPeers.clear();

    //QThread::msleep(1000);//睡眠等待进入析构睡眠
    this->mTcpServer->close();
}

void CommHelper::connectSwitcher()
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->queryPowerStatus();
    }
}

void CommHelper::disconnectSwitcher()
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->logout();
    }
}
/*
 打开电源
*/
void CommHelper::openPower()
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->openSwitcherPOEPower(0x00);
    }
}
/*
 断开电源
*/
void CommHelper::closePower()
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->closeSwitcherPOEPower(0x00);
    }
}

/*
 设置发次信息
*/
#include <QDir>
void CommHelper::setShotInformation(const QString shotDir, const QString shotNum)
{
    this->mShotDir = shotDir;
    this->mShotNum = shotNum;
}


/**
 * @brief 开始测量
 * @param mode 波形测量/能谱测量
 * @param index index=0时则认为是全部探测器一起开始测量
 */
void CommHelper::startMeasure(CommandAdapter::WorkMode mode, quint8 index/* = 0*/)
{
    mWaveAllData.clear();
    mTriggerTimer.clear();

    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            if (mDetectorFileProcessor.contains(index)){
                if (mDetectorFileProcessor[index]->isOpen()){
                    mDetectorFileProcessor[index]->close();
                    mDetectorFileProcessor[index]->deleteLater();
                    mDetectorFileProcessor.remove(index);
                }
            }

            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            if (!detectorDataProcessor->isFreeSocket()){
                detectorDataProcessor->startMeasure(mode);
                emit measureStart(index);
            }
        }
    }
    else if (index >= 1 && index <= DET_NUM){
        if (mDetectorFileProcessor.contains(index)){
            if (mDetectorFileProcessor[index]->isOpen()){
                mDetectorFileProcessor[index]->close();
                mDetectorFileProcessor[index]->deleteLater();
                mDetectorFileProcessor.remove(index);
            }
        }

        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (!detectorDataProcessor->isFreeSocket()){
            detectorDataProcessor->startMeasure(mode);
            emit measureStart(index);
        }
    }
}

/*
 停止测量
*/
void CommHelper::stopMeasure(quint8 index/* = 0*/)
{
    //默认情况所有通道直接一次性停止测量
    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            if (mDetectorFileProcessor.contains(index)){
                DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
                detectorDataProcessor->stopMeasure();

                emit measureStop(index);

                //关闭文件
                if (mDetectorFileProcessor.contains(index)){
                    mDetectorFileProcessor[index]->close();
                    mDetectorFileProcessor[index]->deleteLater();
                    mDetectorFileProcessor.remove(index);
                }
            }
        }
    }
    else if (index >= 1 && index <= DET_NUM){ //关闭单个通道
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        detectorDataProcessor->stopMeasure();

        emit measureStop(index);

        //关闭文件
        if (mDetectorFileProcessor.contains(index)){
            mDetectorFileProcessor[index]->close();
            mDetectorFileProcessor[index]->deleteLater();
            mDetectorFileProcessor.remove(index);
        }
    }
}


/*解析历史文件*/
bool CommHelper::openHistoryWaveFile(const QString &filePath)
{
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)){
        mWaveAllData.clear();

        QVector<quint16> rawWaveData;
        QMap<quint8, QVector<quint16>> realCurve;// 4路通道实测曲线数据
        if (filePath.endsWith(".dat")){                        
            rawWaveData.resize(512);
            for (int i=1; i<=11; ++i){
                int rSize = file.read((char *)rawWaveData.data(), rawWaveData.size() * sizeof(quint16));
                if (rSize == 1024){
                    realCurve[i] = rawWaveData;
                }
            }
        }
        else{
            int chIndex = 1;
            while (!file.atEnd()){
                QByteArray lines = file.readLine();
                lines = lines.replace("\r\n", "");
                QList<QByteArray> listLine = lines.split(',');
                for( auto line : listLine){
                    rawWaveData.push_back(qRound(line.toDouble() * 0.8));
                }

            }
        }

        file.close();
        return true;
    }

    return false;
}


bool copyDir(const QString &src, const QString &dst, bool overwrite = true) {
    QDir srcDir(src);
    if (!srcDir.exists()) return false;

    QDir dstDir(dst);
    if (!dstDir.exists() && !dstDir.mkpath(".")) return false;

    foreach (QFileInfo info, srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        QString dstPath = dst + QDir::separator() + info.fileName();
        if (info.isDir()) {
            if (!copyDir(info.filePath(), dstPath, overwrite)) return false;
        } else {
            if (overwrite && QFile::exists(dstPath)) QFile::remove(dstPath);
            if (!QFile::copy(info.filePath(), dstPath)) return false;
        }
    }
    return true;
}

bool CommHelper::saveAs(QString dstPath)
{
    QString srcPath = QString("%1/%2").arg(mShotDir).arg(mShotNum);
    return copyDir(srcPath, dstPath);
}

qint8 CommHelper::indexOfAddress(QString peerAddress, quint16 peerPort)
{
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();

    //IP和端口全匹配
    {
        QString addr1 = QString("%1:%2").arg(peerAddress).arg(peerPort);
        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
            for (int index = 1; index <= DET_NUM; ++index){
                DetParameter& detParameter = detParameters[index];
                QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
                if (addr2 == addr1 || addr2 == peerAddress){// 匹配IP和端口 或 仅匹配IP也可以
                    return index;
                }
            }

            ++it;
        }
    }

    //仅IP匹配
    {
        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
            for (int index = 1; index <= DET_NUM; ++index){
                DetParameter& detParameter = detParameters[index];
                QString addr2 = QString::fromStdString(detParameter.det_Ip_port);
                if (addr2 == peerAddress){// 匹配IP和端口 或 仅匹配IP也可以
                    return index;
                }
            }

            ++it;
        }
    }

    return -1;
}


void CommHelper::handleDetectorDisconnection(quint8 index)
{
    QMutexLocker locker(&mPeersMutex);
    
    // 根据探测器索引找到对应的连接
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[index];
    QString expectedAddr = QString::fromStdString(detParameter.det_Ip_port);
    
    // 在 mConnectionPeers 中查找对应的连接
    auto it = mConnectionPeers.begin();
    while (it != mConnectionPeers.end()) {
        QTcpSocket* connection = *it;
        QString peerAddress = connection->property("peerAddress").toString();
        quint16 peerPort = connection->property("peerPort").toUInt();
        QString addr = QString("%1:%2").arg(peerAddress).arg(peerPort);
        
        // 匹配探测器地址
        if (addr == expectedAddr || peerAddress == expectedAddr.split(":")[0]) {
            // 找到对应的连接，执行与 disconnected 信号相同的处理
            
            // 1. 触发 disconnectPeerConnection 信号
            QMetaObject::invokeMethod(this, "disconnectPeerConnection", 
                Qt::QueuedConnection, 
                Q_ARG(QString, peerAddress), 
                Q_ARG(quint16, peerPort));
            
            // 2. 触发 detectorOffline 信号
            QMetaObject::invokeMethod(this, "detectorOffline", 
                Qt::QueuedConnection, 
                Q_ARG(quint8, index));
            
            // 3. 取消数据处理器关联
            freeDataProcessor(connection);
            
            // 4. 删除连接并从列表中移除
            connection->deleteLater();
            it = mConnectionPeers.erase(it);
            
            qInfo() << "探测器" << index << "心跳超时，已执行断开连接处理";
            return; // 找到并处理完成，退出
        } else {
            ++it;
        }
    }
    
    // 如果没有找到对应的连接，仍然触发 offline 信号（可能连接已经断开）
    qWarning() << "探测器" << index << "心跳超时，但未找到对应的TCP连接";
    QMetaObject::invokeMethod(this, "detectorOffline", 
        Qt::QueuedConnection, 
        Q_ARG(quint8, index));
}
