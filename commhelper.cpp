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
            QMetaObject::invokeMethod(this, "disconnectPeerConnection", Qt::QueuedConnection, Q_ARG(QString, peerAddress), Q_ARG(quint16, peerPort));

            //根据配置解析是哪一路探测器下线了
            qint8 index = indexOfAddress(peerAddress, peerPort);
            if (index > 0)
                QMetaObject::invokeMethod(this, "detectorOffline", Qt::QueuedConnection, Q_ARG(quint8, index));

            auto it = this->mConnectionPeers.begin();
            while (it != this->mConnectionPeers.end()) {
                if (*it == connection) {
                    //网络掉线了，取消数据处理器关联
                    freeDataProcessor(connection);

                    connection->deleteLater();
                    it = this->mConnectionPeers.erase(it);
                } else {
                    ++it;
                }
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

    mTelnet = new QTelnet(QTelnet::TCP, this);
    mSwitcherStatusRefreshTimer = new QTimer(this);
    connect(mSwitcherStatusRefreshTimer,&QTimer::timeout,this,[=](){
        mSwitcherIsBusy = true;
        // 查看CPU使用率
        if (0){
            QString data = QString("display cpu-usage\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(30000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查看内存使用率
        if (0){
            QString data = QString("display memory-usage\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查看温度
        if (0){
            QString data = QString("display temperature all\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 用来查看风扇的状态
        if (0){
            QString data = QString("display fan\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查询PoE信息
        for (int i=0; i<48; ++i){
            mCurrentQueryPort = i + 1;
            QString data = QString("display poe power-state interface GigabitEthernet0/0/%1\r").arg(mCurrentQueryPort);
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            mSwitcherEventLoop.exec();
        }

        mSwitcherIsBusy = false;
    });

    connect(mTelnet, &QTelnet::socketReadyRead,this,[=](const char *data, int size){
        QByteArray rx_current(data, size);
        mRespondString.append(rx_current);
        if(mRespondString.endsWith("---- More ----")){
            QString data = "\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("[Y/N]:")){
            QString data = "Y\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("Username:")){
            qDebug().noquote() << mRespondString;
            mRespondString.clear();

            QString data = "root\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("Password:")){
            qDebug().noquote() << mRespondString;
            mRespondString.clear();

            QString data = "root@12345\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("<HUAWEI>") && !mSwitcherIsLoginOk){
            mSwitcherIsLoginOk = true;
            qDebug().noquote() << mRespondString;
            mRespondString.clear();

            QString data = "system-view\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("[HUAWEI]") && mSwitcherIsLoginOk && !mSwitcherInSystemView)
        {
            mSwitcherInSystemView = true;
            mRespondString.clear();

            emit switcherConnected();

            if (mCommands.size() > 0){
                QTimer::singleShot(0, this, [=](){
                    mCurrentCommand = mCommands.front();
                    mCommands.pop_front();
                    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
                });
            }
        }
        else if (mSwitcherInSystemView){
            //qDebug().noquote() << mRespondString;
            /*
             * Username:root
             * Password:root@12345
             * <HUAWEI>system-view
             * [HUAWEI]interface GigabitEthernet 0/0/1
             * [HUAWEI-GigabitEthernet0/0/1]poe enable
             * [HUAWEI-GigabitEthernet0/0/1]poe enable
             * Warning: This port is enabled already.
             * [HUAWEI-GigabitEthernet0/0/1]undo poe enable
             * [HUAWEI-GigabitEthernet0/0/1]undo poe enable
             * Warning: This port is disabled already.
            */

            //根据结果来判断
            mRespondString.replace("\r\n","\n");
            // qDebug().noquote() << mRespondString;

            QString warnEnableCmd = "Warning: This port is enabled already.";
            QString warnDisabledCmd = "Warning: This port is disabled already.";
            QString switchCmd = QString("[HUAWEI-GigabitEthernet0/0/%1]").arg(mCurrentQueryPort);
            //QString queryCmd = QString("display poe power-state interface GigabitEthernet0/0/%1").arg(mCurrentQueryPort);
            QString queryCmd = QString("interface GigabitEthernet 0/0/%1\r").arg(mCurrentQueryPort);

            if (mCurrentCommand == queryCmd){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    mRespondString.clear();
                    if (mBatchOn || mSingleOn){
                        QTimer::singleShot(0, this, [=](){
                            mCurrentCommand = "poe enable\r";
                            mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
                        });
                    }
                    else if (mBatchOff || mSingleOff){
                        QTimer::singleShot(0, this, [=](){
                            mCurrentCommand = "undo poe enable\r";
                            mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
                        });
                    }
                }
            }
            else if (mCurrentCommand == "poe enable\r"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();
                    if (lines.size() == 2){
                        //POE打开成功
                        QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, mCurrentQueryPort), Q_ARG(bool, true));
                    }
                    else if (lines.size() == 3){
                        //POE之前已经打开
                        if (lines.at(1) == QString("Warning: This port is enabled already.")){
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, mCurrentQueryPort), Q_ARG(bool, true));
                        }
                    }

                    if (mBatchOn){
                        //打开下一个开关
                        this->openNextSwitcherPOEPower();
                    }
                }
            }
            else if (mCurrentCommand == "undo poe enable\r"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();
                    if (lines.size() == 2){
                        //POE关闭成功
                        QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, mCurrentQueryPort), Q_ARG(bool, false));
                    }
                    else if (lines.size() == 3){
                        //POE之前已经关闭
                        if (lines.at(1) == QString("Warning: This port is disabled already.")){
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, mCurrentQueryPort), Q_ARG(bool, false));
                        }
                    }

                    if (mBatchOff){
                        //关闭下一个开关
                        this->closeNextSwitcherPOEPower();
                    }
                }
            }

            // mSwitcherEventLoop.quit();
            // mRespondString.clear();
        }
        else if (mRespondString.endsWith("]") && mRespondString.contains("[HUAWEI-GigabitEthernet")){
            mSwitcherEventLoop.quit();
            mRespondString.clear();
        }
    });
    connect(mTelnet, &QTelnet::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
        // if(socketState == QAbstractSocket::ConnectedState) {
        //     emit switcherConnected();
        // } else if(socketState == QAbstractSocket::UnconnectedState) {
        //     emit switcherDisconnected();
        // }
    });
    connect(mTelnet, &QTelnet::error, this, [=](QAbstractSocket::SocketError socketError){
        emit switcherDisconnected();
    });

}

void CommHelper::initDataProcessor()
{
    for (int index = 1; index <= DET_NUM; ++index){
        DataProcessor* detectorDataProcessor = new DataProcessor(index, nullptr, this);
        mDetectorDataProcessor[index] = detectorDataProcessor;

        connect(detectorDataProcessor, &DataProcessor::reportStartMeasure, this, [=](){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit measureStart(processor->index());
        });

        connect(detectorDataProcessor, &DataProcessor::reportStopMeasure, this, [=](){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            /*
             * 停止保存
            */
            {
                QMutexLocker locker(&mMutexTriggerTimer);
                if (mDetectorFileProcessor.contains(processor->index())){
                    if (mDetectorFileProcessor[processor->index()]->isOpen()){
                        mDetectorFileProcessor[processor->index()]->flush();
                        mDetectorFileProcessor[processor->index()]->close();
                        mDetectorFileProcessor[processor->index()]->deleteLater();
                        mDetectorFileProcessor.remove(processor->index());
                    }
                }
            }

            emit measureStop(processor->index());
        });

        // 更新温度数据
        connect(detectorDataProcessor, &DataProcessor::reportTemperatureData,
        this, [=](double temperature){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit reportDetectorTemperature(processor->index(), temperature);
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
                    QString filePath = QString("%1/%2/测量数据/%3_%4.dat").arg(mShotDir).arg(mShotNum).arg(mTriggerTimer).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly);

                    qInfo().noquote().nospace() << "[谱仪#"<< processor->index() << "]创建存储文件：" << filePath;
                }

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                    //mDetectorFileProcessor[processor->index()]->flush();
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
                    QString filePath = QString("%1/%2/测量数据/%3_%4.dat").arg(mShotDir).arg(mShotNum).arg(mTriggerTimer).arg(processor->index());
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
bool CommHelper::openSwitcherPOEPower(quint8 port)
{
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
        return false;

    mBatchOn = port == 00 ? true : false;
    mSingleOn = port == 00 ? false : true;
    mBatchOff = false;
    mSingleOff = false;

    // quint8 fromPort = port==0 ? 1 : port;
    // quint8 toPort = port==0 ? DET_NUM : port;
    // for (int i=fromPort; i<=toPort; ++i){
    //     QString cmd = QString("interface GigabitEthernet 0/0/%1").arg(i) + "\r";
    //     mCommands.append(cmd);
    //     cmd = "poe enable\r";
    //     mCommands.append(cmd);
    // }

    mCurrentQueryPort = port==0 ? 1 : port;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    //mCommands.pop_front();
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
    //mSwitcherEventLoop.exec();

    return true;
}

void CommHelper::openNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    mCurrentQueryPort++;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    //mCommands.pop_front();
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
}

void CommHelper::closeNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    mCurrentQueryPort++;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    //mCommands.pop_front();
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
}
/*
 关闭交换机POE口输出电源
*/
bool CommHelper::closeSwitcherPOEPower(quint8 port)
{
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
        return false;

    mBatchOn = false;
    mSingleOn = false;
    mBatchOff = port == 00 ? true : false;
    mSingleOff = port == 00 ? false : true;

    // quint8 fromPort = port==0 ? 1 : port;
    // quint8 toPort = port==0 ? DET_NUM : port;
    // for (int i=fromPort; i<=toPort; ++i){
    //     QString cmd = QString("interface GigabitEthernet 0/0/%1").arg(i) + "\r";
    //     mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
    //     mSwitcherEventLoop.exec();

    //     cmd = "undo poe enable\r";
    //     mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
    //     mSwitcherEventLoop.exec();
    // }

    mCurrentQueryPort = port==0 ? 1 : port;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    //mCommands.pop_front();
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
    //mSwitcherEventLoop.exec();

    return true;
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

/*
 打开电源
*/
void CommHelper::openPower()
{
    mBatchOn = true;
    mSingleOn = false;
    mBatchOff = false;
    mSingleOff = false;
    mCommands.clear();
    quint8 fromPort = 1;
    quint8 toPort = DET_NUM;
    for (int i=fromPort; i<=toPort; ++i){
        QString cmd = QString("interface GigabitEthernet 0/0/%1").arg(i) + "\r";
        mCommands.append(cmd);
        cmd = "poe enable\r";
        mCommands.append(cmd);
    }

    mCurrentQueryPort = 1;
    mTelnet->disconnectFromHost();

    GlobalSettings settings;
    QString hostname = settings.value("Switcher/Telnet/Ip", "192.168.1.253").toString();
    quint16 port = settings.value("Switcher/Telnet/Port", 23).toUInt();
    mTelnet->setType(QTelnet::TCP);
    if (mTelnet->connectToHost(hostname, port)){
        // QString cmd = mCommands.front();
        // mCommands.pop_front();
        // mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
    }
    else
    {
        emit switcherDisconnected();
    }
}
/*
 断开电源
*/
void CommHelper::closePower()
{
    mBatchOn = false;
    mSingleOn = false;
    mBatchOff = true;
    mSingleOff = false;

    if (mTelnet->isConnected()){
        //再发送关闭指令
        this->closeSwitcherPOEPower();
    }

    emit switcherDisconnected();
}

/*
 设置发次信息
*/
#include <QDir>
void CommHelper::setShotInformation(const QString shotDir, const quint32 shotNum)
{
    this->mShotDir = shotDir;
    this->mShotNum = QString::number(shotNum);
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

                    // if (i == 4 || i == 8 || i == 11){
                    //     // 实测曲线
                    //     QMetaObject::invokeMethod(this, [=]() {
                    //         emit showHistoryCurve(realCurve);
                    //     }, Qt::DirectConnection);

                    //     realCurve.clear();
                    // }
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
                    //rawWaveData.push_back(qRound((line.toDouble() - 10996) * 0.9));
                    rawWaveData.push_back(qRound(line.toDouble() * 0.8));
                }

                // if (rawWaveData.size() == 512){
                //     realCurve[chIndex++] = rawWaveData;
                //     // 实测曲线
                //     QMetaObject::invokeMethod(this, [=]() {
                //         emit showHistoryCurve(realCurve);
                //     }, Qt::DirectConnection);

                //     rawWaveData.clear();
                //     realCurve.clear();
                // }
            }

            // 尾巴数据（无效数据）
            // if (rawWaveData.size() > 0){
            //     realCurve[chIndex++] = rawWaveData;
            //     QMetaObject::invokeMethod(this, [=]() {
            //         emit showHistoryCurve(realCurve);
            //     }, Qt::DirectConnection);

            //     rawWaveData.clear();
            //     realCurve.clear();
            // }
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
