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
    if(unfoldData != nullptr ){
        delete unfoldData;
        unfoldData = nullptr;
    }

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
        {
            QString data = QString("display cpu-usage\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(30000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查看内存使用率
        {
            QString data = QString("display memory-usage\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查看温度
        {
            QString data = QString("display temperature all\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 用来查看风扇的状态
        {
            QString data = QString("display fan\r");
            mTelnet->sendData(data.toStdString().c_str(), data.size());
            QTimer::singleShot(3000, &mSwitcherEventLoop, &QEventLoop::quit);
            mSwitcherEventLoop.exec();
        }

        // 查询PoE信息
        for (int i=0; i<48; ++i){
            QString data = QString("display poe power-state interface GigabitEthernet0/0/%1\r").arg(i+1);
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }

        mSwitcherIsBusy = false;
    });

    connect(mTelnet, &QTelnet::socketReadyRead,this,[=](const char *data, int size){
        QByteArray rx_current(data, size);
        if(rx_current.endsWith("---- More ----")){
            QString data = "\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(rx_current.endsWith("[Y/N]:")){
            QString data = "Y\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(rx_current.endsWith("Username:")){
            qDebug().noquote() << rx_current;
            rx_current.clear();

            QString data = "root\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(rx_current.endsWith("Password:")){
            qDebug().noquote() << rx_current;
            rx_current.clear();

            QString data = "root@12345\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(rx_current.endsWith("<HUAWEI>") && !mSwitcherIsLoginOk){
            mSwitcherIsLoginOk = true;
            qDebug().noquote() << rx_current;
            rx_current.clear();

            QString data = "system-view\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(rx_current.endsWith("[HUAWEI]") && mSwitcherIsLoginOk)
        {
            qDebug().noquote() << rx_current;

            rx_current.replace("\r\n","\n");
            if (rx_current.contains("display cpu-usage")){
                //查看CPU使用率
                QList<QByteArray> lines = rx_current.split('\n');
                QMap<QString, QString> mapValue;
                mapValue["Name"] = "CPU Usage";
                for (auto line : lines){
                    if (line.contains("CPU Usage")){
                        QRegularExpression re(":\\s*(\\d+%)");
                        QRegularExpressionMatch match = re.match(line);
                        if (match.hasMatch()) {
                            QString value = match.captured(1).trimmed();
                            mapValue["CPU Usage"] = value;
                        }
                    }
                }

                qDebug().noquote() << "CPU Usage = " << mapValue["CPU Usage"];
                QMetaObject::invokeMethod(this, "reportCPUUsage", Qt::QueuedConnection, Q_ARG(float, mapValue["CPU Usage"].toFloat()));
            }
            else if (rx_current.contains("display memory-usage")){
                //查看内存使用率
                QList<QByteArray> lines = rx_current.split('\n');
                QMap<QString, QString> mapValue;
                for (auto line : lines){
                    if (line.contains("System Total Memory Is")){
                        QString str = "System Total Memory Is: 239075328 bytes";
                        QRegularExpression re("\\d+");
                        QRegularExpressionMatch match = re.match(str);
                        if (match.hasMatch()) {
                            QString numberStr = match.captured(0); // 得到"239075328"
                            //qulonglong number = numberStr.toULongLong(); // 转换为数字
                            mapValue["System Total Memory Is"] = numberStr;
                        }
                    }
                    else if (line.contains("Total Memory Used Is")){
                        QRegularExpression re("\\d+");
                        QRegularExpressionMatch match = re.match(line);
                        if (match.hasMatch()) {
                            QString numberStr = match.captured(0); // 得到"239075328"
                            //qulonglong number = numberStr.toULongLong(); // 转换为数字
                            mapValue["Total Memory Used Is"] = numberStr;
                        }
                    }
                    else if (line.contains("Memory Using Percentage Is")){
                        QStringList parts = QString(line).split(':');
                        if (parts.size() > 1) {
                            QString percentStr = parts[1].trimmed();
                            mapValue["Memory Using Percentage Is"] = percentStr;
                        }
                    }
                }

                qDebug().noquote() << "System Total Memory Is = " << mapValue["System Total Memory Is"] << " bytes";
                qDebug().noquote() << "Total Memory Used Is = " << mapValue["Total Memory Used Is"] << " bytes";
                qDebug().noquote() << "Memory Using Percentage Is = " << mapValue["Memory Using Percentage Is"];
                QMetaObject::invokeMethod(this, "reportSystemTotalMemory", Qt::QueuedConnection, Q_ARG(float, mapValue["System Total Memory Is"].toFloat()));
                QMetaObject::invokeMethod(this, "reportTotalMemoryUsed", Qt::QueuedConnection, Q_ARG(float, mapValue["Total Memory Used Is"].toFloat()));
                QMetaObject::invokeMethod(this, "reportMemoryUsingPercentage", Qt::QueuedConnection, Q_ARG(float, mapValue["Memory Using Percentage Is"].toFloat()));
            }
            else if (rx_current.contains("display temperature all")){
                //查看温度
                QList<QByteArray> lines = rx_current.split('\n');
                QMap<QString, QString> mapValue;
                if (lines.size() == 8){
                    QStringList parts = QString(lines[5]).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    mapValue["Status"] = parts[3].trimmed();
                    mapValue["Current(C)"] = parts[4].trimmed();
                }

                qDebug().noquote() << "Status = " << mapValue["Status"];
                qDebug().noquote() << "Current(C) = " << mapValue["Current(C)"];
                QMetaObject::invokeMethod(this, "reportCurrentTemperature", Qt::QueuedConnection, Q_ARG(float, mapValue["Current(C)"].toFloat()));
            }
            else if (rx_current.contains("display fan")){
                //命令用来查看风扇的状态
                QList<QByteArray> lines = rx_current.split('\n');
                QMap<QString, QString> mapValue;
                if (lines.size() == 7){
                    QStringList parts = QString(lines[4]).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    mapValue["Status0"] = parts[3].trimmed();
                    mapValue["Speed0"] = parts[4].trimmed();

                    parts = QString(lines[5]).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    mapValue["Status1"] = parts[3].trimmed();
                    mapValue["Speed1"] = parts[4].trimmed();
                }

                qDebug().noquote() << "Status0 = " << mapValue["Status0"];
                qDebug().noquote() << "Speed0 = " << mapValue["Speed0"];
                qDebug().noquote() << "Status1 = " << mapValue["Status1"];
                qDebug().noquote() << "Speed1 = " << mapValue["Speed1"];
                QMetaObject::invokeMethod(this, "reportFanSpeed", Qt::QueuedConnection, Q_ARG(float, mapValue["Speed0"].toFloat()), Q_ARG(float, mapValue["Speed1"].toFloat()));
            }
            else if (rx_current.contains("display poe power-state interface")){
                //查看接口GigabitEthernet0/0/x的PoE供电状态信息
                QList<QByteArray> lines = rx_current.split('\n');
                QMap<QString, QString> mapValue;
                mapValue["Name"] = "GigabitEthernet0/0/1";
                for (auto line : lines){
                    QList<QByteArray> values = line.split(':');
                    if (values.size() > 1)
                        mapValue[values[0].trimmed()] = values[1].trimmed();
                    else if (line.contains("display poe power-state interface")){
                        mapValue["Name"] = line.right(QString("GigabitEthernet0/0/0").length());
                    }
                }
                for (auto it = mapValue.constBegin(); it != mapValue.constEnd(); ++it) {
                    qDebug().noquote() << it.key() << " = " << it.value();
                }

                QString name = mapValue["Name"];
                int index = name.right(name.length()-name.lastIndexOf('/')-1).toUInt();
                QMetaObject::invokeMethod(this, "reportFanSpeed", Qt::QueuedConnection, Q_ARG(quint32, index), Q_ARG(bool, mapValue["Power ON/OFF"].toInt()));

                // index--;
                // // 接口名称
                // ui->tableWidget->item(index, 0)->setData(Qt::DisplayRole, mapValue["Name"]);
                // // PoE使能状态
                // ui->tableWidget->item(index, 1)->setData(Qt::DisplayRole, mapValue["Power enable state"]);
                // // 供电开关
                // ui->tableWidget->item(index, 2)->setData(Qt::DisplayRole, mapValue["Power ON/OFF"]);
                // // 供电状态
                // ui->tableWidget->item(index, 3)->setData(Qt::DisplayRole, mapValue["Power status"]);
                // // 最大输出功率
                // ui->tableWidget->item(index, 4)->setData(Qt::DisplayRole, mapValue["Max power(mW)"]);
                // // 当前输出功率
                // ui->tableWidget->item(index, 5)->setData(Qt::DisplayRole, mapValue["Current power(mW)"]);
                // // 峰值功率
                // ui->tableWidget->item(index, 6)->setData(Qt::DisplayRole, mapValue["Peak power(mW)"]);
                // // 平均功率
                // ui->tableWidget->item(index, 7)->setData(Qt::DisplayRole, mapValue["Average power(mW)"]);
                // // 电流
                // ui->tableWidget->item(index, 8)->setData(Qt::DisplayRole, mapValue["Current(mA)"]);
                // // 电压
                // ui->tableWidget->item(index, 9)->setData(Qt::DisplayRole, mapValue["Voltage(V)"]);
            }

            mSwitcherEventLoop.quit();
            rx_current.clear();
        }
        else if (rx_current.endsWith("]") && rx_current.contains("[HUAWEI-GigabitEthernet")){
            mSwitcherEventLoop.quit();
            rx_current.clear();
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

        connect(detectorDataProcessor, &DataProcessor::reportSpectrumData, this, [=](QByteArray data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit reportSpectrumData(processor->index(), data);
        });

        connect(detectorDataProcessor, &DataProcessor::reportWaveformData, this, [=](QByteArray data){
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
                    mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), 1040/*data.size()*/);
                    //mDetectorFileProcessor[processor->index()]->flush();
                }
            }

            bool ok;
            quint16 serialNumber = data.mid(4, 2).toHex().toUShort(&ok, 16);//设备编号
            data.remove(0, 8);//移除包头
            data.chop(8);//移除包尾
            emit reportWaveformData(processor->index(), data);
        });

        connect(detectorDataProcessor, &DataProcessor::reportParticleData, this, [=](QByteArray data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit reportParticleData(processor->index(), data);
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

    auto it = this->mConnectionPeers.begin();
    while (it != this->mConnectionPeers.end()) {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString addr2 = QString::fromStdString(detParameter.detIp);
            if (addr1 == addr2){
                mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                return;
            }
        }

        ++it;
    }

    //重新分配一个之前从没绑定过的数据处理器
    for (int index = 1; index <= DET_NUM; ++index){
        if (mDetectorDataProcessor[index]->isFreeSocket()){
            DetParameter& detParameter = detParameters[index];
            QString addr2 = QString::fromStdString(detParameter.detIp);
            if (addr2 == "0.0.0.0:6000"){
                //给新上线网络连接分配一个空闲的探测器
                qstrcpy(detParameter.detIp, addr1.toStdString().c_str());
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
            qstrcpy(detParameter.detIp, addr1.toStdString().c_str());
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
            QString addr2 = QString::fromStdString(detParameter.detIp);
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
void CommHelper::openSwitcherPOEPower()
{
    if (nullptr == mTelnet || !mTelnet->isConnected())
        return;

    for (int i=1; i<DET_NUM; ++i){
        QString cmd = QString("interface GigabitEthernet 0/0/%1").arg(i) + "\r";
        mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
        mSwitcherEventLoop.exec();

        cmd = "poe enable\r";
        mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
        mSwitcherEventLoop.exec();
    }
}


/*
 关闭交换机POE口输出电源
*/
void CommHelper::closeSwitcherPOEPower()
{
    if (nullptr == mTelnet || !mTelnet->isConnected())
        return;

    for (int i=1; i<DET_NUM; ++i){
        QString cmd = QString("interface GigabitEthernet 0/0/%1").arg(i) + "\r";
        mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
        mSwitcherEventLoop.exec();

        cmd = "undo poe enable\r";
        mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
        mSwitcherEventLoop.exec();
    }
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
    GlobalSettings settings;
    QString hostname = settings.value("Switcher/Telnet/Ip", "192.168.1.253").toString();
    quint16 port = settings.value("Switcher/Telnet/Port", 23).toUInt();
    mTelnet->setType(QTelnet::TCP);
    if (mTelnet->connectToHost(hostname, port)){
        emit switcherConnected();

        this->openSwitcherPOEPower();
    }
}
/*
 断开电源
*/
void CommHelper::closePower()
{
    if (mTelnet->isConnected()){
        //再发送关闭指令
        this->closeSwitcherPOEPower();
    }
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


/*
 开始测量
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
            detectorDataProcessor->startMeasure(mode);                         
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
        detectorDataProcessor->startMeasure(mode);
    }
}

/*
 停止测量
*/
void CommHelper::stopMeasure(quint8 index/* = 0*/)
{
    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            detectorDataProcessor->stopMeasure();
        }
    }
    else if (index >= 1 && index <= DET_NUM){
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        detectorDataProcessor->stopMeasure();
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

    QString addr1 = QString("%1:%2").arg(peerAddress).arg(peerPort);
    auto it = this->mConnectionPeers.begin();
    while (it != this->mConnectionPeers.end()) {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString addr2 = QString::fromStdString(detParameter.detIp);
            if (addr2 == addr1){
                return index;
            }
        }

        ++it;
    }

    return -1;
}
