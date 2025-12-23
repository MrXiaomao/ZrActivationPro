#ifndef QHUAWEISWITCHERHELPER_H
#define QHUAWEISWITCHERHELPER_H

#include <QObject>
#include <QEventLoop>
#include <QTimer>
#include <QMap>

class QTelnet;
class QHuaWeiSwitcherHelper : public QObject
{
    Q_OBJECT
public:
    explicit QHuaWeiSwitcherHelper(QString ip, QObject *parent = nullptr);

    //判断谱仪编号是否存在
    bool contains(quint8 index);

    //根据POE端口号找谱仪编号
    qint8 indexOfPort(quint8 port);

    //根据谱仪编号找POE端口号
    qint8 portOfIndex(quint8 index);

    void setAssociatedDetector(QString);

    /*
     连接并查询电源状态
    */
    Q_SLOT void queryPowerStatus();
    /*********************************************************
     交换机指令
    ***********************************************************/
    /*
     打开交换机POE口输出电源
    */
    Q_SLOT bool openSwitcherPOEPower(quint8 port = 0);
    Q_SLOT void openNextSwitcherPOEPower();
    Q_SLOT void queryNextSwitcherPOEPower();

    /*
     关闭交换机POE口输出电源
    */
    Q_SLOT bool closeSwitcherPOEPower(quint8 port = 0);
    Q_SLOT void closeNextSwitcherPOEPower();

    Q_SIGNAL void switcherConnected(QString);//交换机连接
    Q_SIGNAL void switcherDisconnected(QString);//交换机断开
    Q_SIGNAL void reportPoePowerStatus(quint8, bool); //POE电源开关

signals:


private:
    QTelnet *mTelnet = nullptr;//华为交换机控制POE电源
    QTimer *mStatusRefreshTimer = nullptr;//状态定时刷新时钟
    QString mIp;
    QEventLoop mSwitcherEventLoop;
    quint8 mCurrentQueryPort = 0;
    bool mSwitcherIsBusy = false;
    bool mSwitcherIsLoginOk = false;// 交换机是否登录成功
    bool mSwitcherInSystemView = false;// 交换机是否处于视图模式
    bool mBatchOn = false;//批量开
    bool mSingleOn = false;//单开
    bool mBatchOff = false;//批量关
    bool mSingleOff = false;//批量关
    QList<QString> mCommands;
    QString mCurrentCommand;
    QByteArray mRespondString;
    QMap<quint8/*port-POE端口号*/, quint8/*index-谱仪编号*/> mMapAssociatedDetector;
};

#endif // QHUAWEISWITCHERHELPER_H
