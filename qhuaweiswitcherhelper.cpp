#include "qhuaweiswitcherhelper.h"
#include "globalsettings.h"
#include "QTelnet.h"

QHuaWeiSwitcherHelper::QHuaWeiSwitcherHelper(QString ip, QObject *parent)
    : QObject{parent}
    , mIp(ip)
{
    mTelnet = new QTelnet(QTelnet::TCP, this);
    mStatusRefreshTimer = new QTimer(this);
    connect(mStatusRefreshTimer,&QTimer::timeout,this,[=](){
        if (!mTelnet->isConnected())
            return;

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
        if (0){
            for (int i=0; i<48; ++i){
                mCurrentQueryPort = i + 1;
                QString data = QString("display poe power-state interface GigabitEthernet0/0/%1\r").arg(mCurrentQueryPort);
                mTelnet->sendData(data.toStdString().c_str(), data.size());
                mSwitcherEventLoop.exec();
            }
        }

        //模拟一个回车键，让其快速返回吧
        QString data = QString("\r");
        mTelnet->sendData(data.toStdString().c_str(), data.size());

        mSwitcherIsBusy = false;
    });
    mStatusRefreshTimer->start(300000);/*5分钟刷新一次吧*/

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

            emit switcherConnected(mIp);

            QTimer::singleShot(0, this, [=](){
                mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
            });
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
    });

    connect(mTelnet, &QTelnet::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
        // if(socketState == QAbstractSocket::ConnectedState) {
        //     emit switcherConnected(mIp);
        // } else if(socketState == QAbstractSocket::UnconnectedState) {
        //     emit switcherDisconnected(mIp);
        // }
    });

    connect(mTelnet, &QTelnet::error, this, [=](QAbstractSocket::SocketError socketError){
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        emit switcherDisconnected(mIp);
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
        emit switcherDisconnected(mIp);
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
