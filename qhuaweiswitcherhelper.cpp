#include "qhuaweiswitcherhelper.h"
#include "globalsettings.h"
#include "QTelnet.h"

QHuaWeiSwitcherHelper::QHuaWeiSwitcherHelper(QString ip, QObject *parent)
    : QObject{parent}
    , mIp(ip)
{
    mTelnet = new QTelnet(QTelnet::TCP, this);
    
    // 初始化心跳检测
    mHeartbeatTimer = new QTimer(this);
    mHeartbeatTimer->setInterval(30000); // 每30秒检测一次
    mHeartbeatTimer->setSingleShot(false);
    connect(mHeartbeatTimer, &QTimer::timeout, this, &QHuaWeiSwitcherHelper::performHeartbeatCheck);
    mWaitingHeartbeatResponse = false;
    mIsReconnecting = false;
    
    connect(mTelnet, &QTelnet::socketReadyRead,this,[=](const char *data, int size){
        QByteArray rx_current(data, size);
        mRespondString.append(rx_current);
        //qDebug() << mRespondString;
        if(mRespondString.endsWith("---- More ----")){
            QString data = "\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("[Y/N]:")){
            QString data = "Y\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("Username:")){
            mRespondString.clear();

            QString data = "root\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("Password:")){
            mRespondString.clear();

            QString data = "root@12345\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("<HUAWEI>") && !mSwitcherIsLoginOk){
            mSwitcherIsLoginOk = true;
            mRespondString.clear();

            QString data = "system-view\r";
            mTelnet->sendData(data.toStdString().c_str(), data.size());
        }
        else if(mRespondString.endsWith("[HUAWEI]") && mSwitcherIsLoginOk && !mSwitcherInSystemView)
        {
            mSwitcherInSystemView = true;
            mRespondString.clear();
            
            // 如果是重连成功，重置重连标志
            if (mIsReconnecting) {
                mIsReconnecting = false;
                qInfo() << "交换机" << mIp << "重连成功";
            }

            emit switcherConnected(mIp);
            
            // 登录成功后启动心跳检测
            startHeartbeatCheck();

            QTimer::singleShot(0, this, [=](){
                mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
            });
        }
        // 检测心跳响应（只要在等待心跳响应且响应包含 HUAWEI 提示符就认为是心跳响应）
        else if (mWaitingHeartbeatResponse &&
                 (mRespondString.contains("[HUAWEI]") || mRespondString.contains("<HUAWEI>") || mRespondString.contains("display clock\r")))
        {
            // 收到心跳响应，重置标志
            mWaitingHeartbeatResponse = false;
            mLastHeartbeatTime.restart();
            mRespondString.clear();
            qDebug() << "交换机" << mIp << "心跳检测正常";
        }
        else if (mSwitcherInSystemView){
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

            QString warnEnableCmd = "Warning: This port is enabled already.";
            QString warnDisabledCmd = "Warning: This port is disabled already.";
            QString switchCmd = QString("[HUAWEI-GigabitEthernet0/0/%1]").arg(mCurrentQueryPort);//进入interface GigabitEthernet之后
            QString switchCmd2 = QString("<HUAWEI>");//进入视图之前
            QString switchCmd3 = QString("[HUAWEI]");//进入视图之后
            QString queryCmd = QString("display poe power-state interface GigabitEthernet0/0/%1\r").arg(mCurrentQueryPort);
            QString enterInterfaceCmd = QString("interface GigabitEthernet 0/0/%1\r").arg(mCurrentQueryPort);

            if (mCurrentCommand == queryCmd){
                if (mRespondString.endsWith(switchCmd3.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();

                    QMap<QString, QString> mapValues;
                    for (auto line : lines)
                    {
                        QList<QByteArray> values = line.split(':');
                        if (values.size() > 1)
                            mapValues[values[0].trimmed()] = values[1].trimmed();
                    }

                    qint8 index = this->indexOfPort(mCurrentQueryPort);
                    if (index > 0)
                    {
                        if (mapValues["Power enable state"] == "enable")
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                        else
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                    }

                    // 查询下一个POE供电口状态
                    queryNextSwitcherPOEPower();
                }
            }
            else if (mCurrentCommand == enterInterfaceCmd){
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
                        qint8 index = this->indexOfPort(mCurrentQueryPort);
                        if (index > 0)
                        {
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                        }
                    }
                    else if (lines.size() == 3){
                        //POE之前已经打开
                        if (lines.at(1) == QString("Warning: This port is enabled already.")){
                            qint8 index = this->indexOfPort(mCurrentQueryPort);
                            if (index > 0)
                            {
                                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                            }
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
                        qint8 index = this->indexOfPort(mCurrentQueryPort);
                        if (index > 0)
                        {
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                        }
                    }
                    else if (lines.size() == 3){
                        //POE之前已经关闭
                        if (lines.at(1) == QString("Warning: This port is disabled already.")){
                            qint8 index = this->indexOfPort(mCurrentQueryPort);
                            if (index > 0)
                            {
                                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                            }
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

        if (mRespondString.contains("<HUAWEI>") || mRespondString.contains("[HUAWEI]"))
        {
            // Telnet 会话仍然存活
        }
        else if (mRespondString.contains("Username:")
            || mRespondString.contains("Password:"))
        {
            // 会话已失效，被踢回登录
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            stopHeartbeatCheck();  // 停止心跳检测
            qInfo() << "交换机" << mIp << "会话失效，重新登录";
            emit switcherDisconnected(mIp);
        }
    });

    connect(mTelnet, &QTelnet::error, this, [=](QAbstractSocket::SocketError socketError){
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        stopHeartbeatCheck();  // 停止心跳检测
        qWarning() << "交换机" << mIp << "连接错误，错误码：" << socketError;
        emit switcherDisconnected(mIp);
    });
    
    // 监听连接状态变化，在断开时停止心跳检测
    connect(mTelnet, &QTelnet::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
        if (socketState == QAbstractSocket::UnconnectedState) {
            stopHeartbeatCheck();
            qInfo() << "交换机" << mIp << "连接断开";
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
        } else if (socketState == QAbstractSocket::ConnectedState && mSwitcherInSystemView) {
            // 如果已经登录成功，重新启动心跳检测
            startHeartbeatCheck();
        }
    });

}

//判断谱仪编号是否存在
bool QHuaWeiSwitcherHelper::contains(quint8 index)
{
    if (index == 0x00)
        return true;
    else
    {
        quint8 port = mMapAssociatedDetector.key(index, 0);
        return port != 0x00;
    }

    return false;
}

//根据POE端口号找谱仪编号
qint8 QHuaWeiSwitcherHelper::indexOfPort(quint8 port)
{
    if (mMapAssociatedDetector.contains(port))
        return mMapAssociatedDetector[port];
    else
        return -1;
}

//根据谱仪编号找POE端口号
qint8 QHuaWeiSwitcherHelper::portOfIndex(quint8 index)
{
    if (index == 0x00)
        return 0x00;
    else
    {
        quint8 port = mMapAssociatedDetector.key(index, 0);
        return port ;
    }

    return -1;
}

void QHuaWeiSwitcherHelper::setAssociatedDetector(QString text)
{
    QStringList lines = text.split(',');
    for (auto line : lines)
    {
        QStringList values = line.split('-');
        if (values.size() > 1)
            mMapAssociatedDetector[values[0].trimmed().toUInt()] = values[1].trimmed().toUInt();
    }
}
/*
 连接并查询电源状态
*/
void QHuaWeiSwitcherHelper::queryPowerStatus()
{
    mBatchOn = true;
    mSingleOn = false;
    mBatchOff = false;
    mSingleOff = false;
    quint8 fromPort = 1;
    quint8 toPort = DET_NUM;

    //开机仅查询POE供电
    mCurrentQueryPort = 1;
    mCurrentCommand = "display poe power-state interface GigabitEthernet0/0/1\r";
    mTelnet->disconnectFromHost();
    mTelnet->setType(QTelnet::TCP);
    if (mTelnet->connectToHost(mIp, 23)){

    }
    else
    {
        //emit switcherDisconnected(mIp);
    }
}

/*********************************************************
     交换机指令
    ***********************************************************/
/*
 打开交换机POE口输出电源
*/
bool QHuaWeiSwitcherHelper::openSwitcherPOEPower(quint8 index/* = 0*/)
{
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
        return false;

    mBatchOn = index == 00 ? true : false;
    mSingleOn = index == 00 ? false : true;
    mBatchOff = false;
    mSingleOff = false;

    mCurrentQueryPort = index==0 ? 1 : index;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());

    return true;
}

void QHuaWeiSwitcherHelper::openNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    mCurrentQueryPort++;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
}

void QHuaWeiSwitcherHelper::queryNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    mCurrentQueryPort++;
    mCurrentCommand = QString("display poe power-state interface GigabitEthernet0/0/%1").arg(mCurrentQueryPort) + "\r";
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
}

/*
 关闭交换机POE口输出电源
*/
bool QHuaWeiSwitcherHelper::closeSwitcherPOEPower(quint8 port/* = 0*/)
{
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
        return false;

    mBatchOn = false;
    mSingleOn = false;
    mBatchOff = port == 00 ? true : false;
    mSingleOff = port == 00 ? false : true;

    mCurrentQueryPort = port==0 ? 1 : port;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());

    return true;
}

void QHuaWeiSwitcherHelper::closeNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    mCurrentQueryPort++;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r";
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
}

//是否仍然处于登录状态
void QHuaWeiSwitcherHelper::checkLoginAlive()
{
    if (!mTelnet->isConnected()) {
        mSwitcherIsLoginOk = false;
        return;
    }

    QString cmd = "display clock\r";
    mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
}

// 启动心跳检测
void QHuaWeiSwitcherHelper::startHeartbeatCheck()
{
    if (mHeartbeatTimer && !mHeartbeatTimer->isActive()) {
        mLastHeartbeatTime.start();
        mWaitingHeartbeatResponse = false;
        mHeartbeatTimer->start();
        qInfo() << "交换机" << mIp << "心跳检测已启动";
    }
}

// 停止心跳检测
void QHuaWeiSwitcherHelper::stopHeartbeatCheck()
{
    if (mHeartbeatTimer && mHeartbeatTimer->isActive()) {
        mHeartbeatTimer->stop();
        mWaitingHeartbeatResponse = false;
        qInfo() << "交换机" << mIp << "心跳检测已停止";
    }
}

// 执行心跳检测
void QHuaWeiSwitcherHelper::performHeartbeatCheck()
{
    // 如果正在重连中，跳过本次检测
    if (mIsReconnecting) {
        return;
    }
    
    // 如果不在系统视图或未连接，尝试重连
    if (!mTelnet->isConnected() || !mSwitcherInSystemView) {
        qInfo() << "交换机" << mIp << "连接状态异常，尝试重连";
        reconnectSwitcher();
        return;
    }
    
    // 如果正在等待上一次心跳响应，检查是否超时
    if (mWaitingHeartbeatResponse) {
        // 如果超过10秒没有收到响应，认为掉线
        if (mLastHeartbeatTime.elapsed() > 10000) {
            qInfo() << "交换机" << mIp << "心跳响应超时，可能已掉线，尝试重连";
            reconnectSwitcher();
            return;
        }
    }
    
    // 发送心跳命令
    if (!mSwitcherIsBusy) {
        mCurrentCommand = "display clock\r";
        mWaitingHeartbeatResponse = true;
        mLastHeartbeatTime.restart();
        mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
        qDebug() << "交换机" << mIp << "发送心跳检测命令";
    }
}

// 重连交换机
void QHuaWeiSwitcherHelper::reconnectSwitcher()
{
    // 防止重复重连
    if (mIsReconnecting) {
        return;
    }
    
    mIsReconnecting = true;
    stopHeartbeatCheck();
    
    qInfo() << "交换机" << mIp << "开始重连...";
    
    // 断开当前连接
    if (mTelnet->isConnected()) {
        mTelnet->disconnectFromHost();
    }
    
    // 重置状态
    mSwitcherIsLoginOk = false;
    mSwitcherInSystemView = false;
    mRespondString.clear();
    
    // 延迟一下再重连，避免立即重连失败
    QTimer::singleShot(2000, this, [=](){
        mTelnet->setType(QTelnet::TCP);
        if (mTelnet->connectToHost(mIp, 23)) {
            qInfo() << "交换机" << mIp << "重连请求已发送";
        } else {
            qWarning() << "交换机" << mIp << "重连失败，将在下次心跳检测时重试";
            mIsReconnecting = false;
            // 如果重连失败，延迟一段时间后再次尝试
            QTimer::singleShot(30000, this, [=](){
                mIsReconnecting = false;
            });
        }
    });
}
