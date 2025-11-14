#ifndef COMMANDADAPTER_H
#define COMMANDADAPTER_H

#include <QObject>
#include <QTcpSocket>
#include <QtMath>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <qlitethread.h>

class CommandAdapter : public QObject
{
    Q_OBJECT
public:
    explicit CommandAdapter(QObject *parent = nullptr);
    ~CommandAdapter();

    virtual void sendCmdToSocket(QByteArray&) const{};

    Q_SIGNAL void reportStartMeasure();
    Q_SIGNAL void reportStopMeasure();
    Q_SIGNAL void reportSpectrumData(QByteArray&);
    Q_SIGNAL void reportWaveformData(QByteArray&);
    Q_SIGNAL void reportParticleData(QByteArray&);

signals:

public:
    /*********************************************************
     通用基础配置
    ***********************************************************/

    //增益指令
    void sendGain(bool isRead = true, quint16 gain = 0x0000){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[8] = ((gain >> 8) & 0xFF); // 高字节
        askCurrentCmd[9] = (gain & 0xFF);        // 低字节
        pushCmd(askCurrentCmd);
    }

    //死时间配置(ns)
    void sendDeathTime(bool isRead = true, quint16 deathTime = 300){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 11 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[8] = ((deathTime >> 8) & 0xFF); // 高字节
        askCurrentCmd[9] = (deathTime & 0xFF);        // 低字节
        pushCmd(askCurrentCmd);
    }

    //触发阈值
    void sendTriggerThold(bool isRead = true, quint16 triggerThold = 100){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 12 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[8] = ((triggerThold >> 8) & 0xFF); // 高字节
        askCurrentCmd[9] = (triggerThold & 0xFF);        // 低字节
        pushCmd(askCurrentCmd);
    }

    /*********************************************************
     网络通讯配置
    ***********************************************************/
    //FPGA的IP地址修改
    void sendIpAddress(bool isRead = true, QString ipAddress = "0.0.0.0"){
        QStringList addrs = ipAddress.split('.');
        if (addrs.size() != 4)
            return;

        this->sendMacAddress(isRead, addrs[0].toShort(), addrs[1].toShort(), addrs[2].toShort(), addrs[3].toShort());
    };
    void sendIpAddress(bool isRead = true, quint8 addr1 = 0x00, quint8 addr2 = 0x00, quint8 addr3 = 0x00, quint8 addr4 = 0x00){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FB 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[6] = addr1;
        askCurrentCmd[7] = addr2;
        askCurrentCmd[8] = addr3;
        askCurrentCmd[9] = addr4;
        pushCmd(askCurrentCmd);
    }

    //FPGA的MAC地址修改"00-08-xx-xx-xx-xx"
    void sendMacAddress(bool isRead = true, QString macAddress = "00-00-00-00-00-00"){
        QStringList addrs = macAddress.split('-');
        if (addrs.size() != 6)
            return;

        this->sendMacAddress(isRead, addrs[2].toShort(), addrs[3].toShort(), addrs[4].toShort(), addrs[5].toShort());
    };
    void sendMacAddress(bool isRead = true, quint8 addr1 = 0x00, quint8 addr2 = 0x00, quint8 addr3 = 0x00, quint8 addr4 = 0x00){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FB 11 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[6] = addr1;
        askCurrentCmd[7] = addr2;
        askCurrentCmd[8] = addr3;
        askCurrentCmd[9] = addr4;
        pushCmd(askCurrentCmd);
    }

    //服务端通讯地址修改
    void sendServerAddress(bool isRead = true, QString ipAddress = "0.0.0.0", QString maskAddressr = "0.0.0.0", QString gatewayAddress = "0.0.0.0", quint32 port = 0){
        QStringList addrs = ipAddress.split('.');
        if (addrs.size() != 4)
            return;

        quint8 ip1 = addrs[0].toShort();
        quint8 ip2 = addrs[1].toShort();
        quint8 ip3 = addrs[2].toShort();
        quint8 ip4 = addrs[3].toShort();

        addrs = maskAddressr.split('.');
        if (addrs.size() != 4)
            return;

        quint8 mask1 = addrs[0].toShort();
        quint8 mask2 = addrs[1].toShort();
        quint8 mask3 = addrs[2].toShort();
        quint8 mask4 = addrs[3].toShort();

        addrs = gatewayAddress.split('.');
        if (addrs.size() != 4)
            return;

        quint8 gateway1 = addrs[0].toShort();
        quint8 gateway2 = addrs[1].toShort();
        quint8 gateway3 = addrs[2].toShort();
        quint8 gateway4 = addrs[3].toShort();

        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FB 12 00 00 00 00 00 00 00 00 00 00 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[6] = ip1;
        askCurrentCmd[7] = ip2;
        askCurrentCmd[8] = ip3;
        askCurrentCmd[9] = ip4;

        askCurrentCmd[10] = mask1;
        askCurrentCmd[11] = mask2;
        askCurrentCmd[12] = mask3;
        askCurrentCmd[13] = mask4;

        askCurrentCmd[14] = gateway1;
        askCurrentCmd[15] = gateway2;
        askCurrentCmd[16] = gateway3;
        askCurrentCmd[17] = gateway4;

        askCurrentCmd[18] = ((port >> 8) & 0xFF); // 高字节
        askCurrentCmd[19] = (port & 0xFF);        // 低字节

        pushCmd(askCurrentCmd);
    }

    //UDP通讯地址修改
    void sendUdpAddress(bool isRead = true, QString udpAddress = "0.0.0.0"){
        QStringList addrs = udpAddress.split('.');
        if (addrs.size() != 6)
            return;

        this->sendUdpAddress(isRead, addrs[2].toShort(), addrs[3].toShort(), addrs[4].toShort(), addrs[5].toShort());
    };
    void sendUdpAddress(bool isRead = true, quint8 addr1 = 0x00, quint8 addr2 = 0x00, quint8 addr3 = 0x00, quint8 addr4 = 0x00){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FB 13 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else
            askCurrentCmd[3] = 0x0F;
        askCurrentCmd[6] = addr1;
        askCurrentCmd[7] = addr2;
        askCurrentCmd[8] = addr3;
        askCurrentCmd[9] = addr4;
        pushCmd(askCurrentCmd);
    }

    /*********************************************************
     波形基本配置
    ***********************************************************/
    enum TriggerMode{
        tmTimer = 0x00,//定时触发模式
        tmNormal = 0x01//正常触发模式
    };
    enum WaveformLength{
        wl64 = 0x00,    //波形长度64
        wl128 = 0x01,   //波形长度128
        wl256 = 0x02,   //波形长度256
        wl512 = 0x03    //波形长度512
    };
    void sendWaveformMode(bool isRead = true, TriggerMode triggerMode = tmTimer, quint16 waveformLength = 64){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FC 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            mWaveformLength = waveformLength;
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[7] = triggerMode;
            switch (waveformLength) {
            case 64:
                askCurrentCmd[9] = wl64;
                break;
            case 128:
                askCurrentCmd[9] = wl128;
                break;
            case 256:
                askCurrentCmd[9] = wl256;
                break;
            case 512:
                askCurrentCmd[9] = wl512;
                break;
            default:
                break;
            }
        }

        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     能谱基本配置
    ***********************************************************/
    //能谱刷新时间修改(ms，默认值1000)
    void sendSprectnumRefreshTimelength(bool isRead = true, quint32 spectrumRefreshTime = 1000){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FD 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[6] = (spectrumRefreshTime >> 24) & 0xFF;
            askCurrentCmd[7] = (spectrumRefreshTime >> 16) & 0xFF;
            askCurrentCmd[8] = (spectrumRefreshTime >> 8)  & 0xFF;
            askCurrentCmd[9] = (spectrumRefreshTime)       & 0xFF;
        }
        pushCmd(askCurrentCmd);
    };

    //能谱长度修改
    enum SpectrumLength{
        sl1024 = 0x00, //能谱长度1024；
        sl2048 = 0x01, //能谱长度2048；
        sl4096 = 0x02, //能谱长度4096；
        sl8192 = 0x03  //能谱长度8192；默认值为0001
    };
    void sendSpectrumLength(bool isRead = true, quint16 spectrumLength = 1024){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FD 11 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            switch (spectrumLength){
            case 1024: askCurrentCmd[9] = sl1024; break;
            case 2048: askCurrentCmd[9] = sl2048; break;
            case 4096: askCurrentCmd[9] = sl4096; break;
            case 8192: askCurrentCmd[9] = sl8192; break;
            }
        }
        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     梯形成型基本配置
    ***********************************************************/
    //梯形成型时间常数配置
    void sendTrapTimeConst(bool isRead = true, float f1 = 0x00, float f2 = 0x00){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            quint16 d1 = qFloor(f1 * 65536);
            quint16 d2 = qFloor(f2 * 65536);
            if (d1 >= d2)
                return;

            askCurrentCmd[6] = ((d1 >> 8) & 0xFF);
            askCurrentCmd[7] = (d1 & 0xFF);
            askCurrentCmd[8] = ((d2 >> 8) & 0xFF);
            askCurrentCmd[9] = (d2 & 0xFF);
        }
        pushCmd(askCurrentCmd);
    };

    //上升沿、平顶、下降沿长度配置
    void sendRisePeakFallPoints(bool isRead = true, quint8 rise = 0x0f, quint8 peak = 0x0f, quint8 fall = 0x0f){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 11 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[7] = rise;
            askCurrentCmd[8] = peak;
            askCurrentCmd[9] = fall;
        }
        pushCmd(askCurrentCmd);
    };

    //梯形成型使能配置
    enum TrapShapeEnable{
        teClose = 0x00,//关闭
        teOpen = 0x01 //打开
    };
    void sendTrapShapeEnable(bool isRead = true, TrapShapeEnable trapShapeEnable = teOpen){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 12 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[9] = trapShapeEnable;
        }
        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     高压电源配置
    ***********************************************************/
    //高压电平使能配置
    void sendHighVolgateOutLevel(bool isRead = true, quint16 highVolgateOutLevel = 0x00){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F F9 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[8] = ((highVolgateOutLevel >> 8) & 0xFF);
            askCurrentCmd[9] = (highVolgateOutLevel & 0xFF);
        }
        pushCmd(askCurrentCmd);
    };

    enum HighVolgateOutLevelEnable{
        hvClose = 0x00,//关闭
        hvOpen = 0x01 //打开
    };
    void sendHighVolgateOutLevelEnable(bool isRead = true, HighVolgateOutLevelEnable highVolgateOutLevelEnable = hvClose){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F F9 11 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[9] = highVolgateOutLevelEnable;
        }
        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     工作模式配置
    ***********************************************************/
    enum WorkMode{
        wmWaveform = 0x00,//波形
        wmSpectrum = 0x01,//能谱
        wmParticle = 0x02 //粒子
    };
    enum DataType{
        dtWaveform = 0x00D1,//波形
        dtSpectrum = 0x00D2,//能谱
        dtParticle = 0x00D3 //粒子
    };
    void sendWorkMode(bool isRead = true, WorkMode workMode = wmSpectrum){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FF 10 00 00 00 00 AB CD").toUtf8());
        if (isRead)
            askCurrentCmd[3] = 0x0A;
        else{
            askCurrentCmd[3] = 0x0F;
            askCurrentCmd[9] = workMode;
        }
        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     控制类指令
    ***********************************************************/
    //开始测量
    void sendStartMeasure(){
        mValidDataPkgRef = 0;
        mAskStopMeasure = false;
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 01 AB CD").toUtf8());
        pushCmd(askCurrentCmd);

        mIsMeasuring = true;

        //通知发送指令
        this->notifySendNextCmd();
    };

    //停止测量
    void sendStopMeasure(){        
        /*强制清空之前的指令*/
        this->clear();

        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 00 AB CD").toUtf8());
        pushCmd(askCurrentCmd);

        mAskStopMeasure = true;
        mIsMeasuring = true;

        //通知发送指令
        this->notifySendNextCmd();
    };

    /*********************************************************
     应答类指令
    ***********************************************************/
    //设备查询指令-程序版本号查询
    void sendSearchAppversion(){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0A DA 10 00 00 00 00 AB CD").toUtf8());
        pushCmd(askCurrentCmd);
    };

    void sendPluse(){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0C DA 11 00 00 00 00 AB CD").toUtf8());
        pushCmd(askCurrentCmd);
    };

    /*********************************************************
     OTA更新指令
    ***********************************************************/
    //Flash地址设置
    //OTA更新数据传输指令

    //重加载FPGA程序
    enum HostProgram{
        haMain = 0x00,//主程序
        hpBakup = 0x01//备用程序
    };
    void sendSwitchHost(HostProgram hostProgram = haMain){
        QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F CA 12 00 00 00 00 AB CD").toUtf8());
        askCurrentCmd[9] = hostProgram;
        pushCmd(askCurrentCmd);
    }

    /*
     * 发送指令
    */
    void pushCmd(QByteArray& askCmd);

    /*
     * 发送下一条指令
    */
    void notifySendNextCmd();

    /*
     清空指令
    */
    void clear();

protected:
    bool mIsMeasuring = false;//测量是否正在进行中
    void analyzeCommands(QByteArray &cachePool);

private:    
    bool mAskStopMeasure = false; //是否请求结束测量(如果已经请求了结束测量，那么就有必要解析结束测量指令)
    QQueue<QByteArray> mCmdQueue;//发送指令等候区

    quint16 mWaveformLength = 512;//波形或能谱长度
    quint32 mValidDataPkgRef = 0;//有效数据包个数

    QMutex mCmdMutex;
    bool mTerminatedThread = false;//线程退出标识
    bool mCmdReady = false;//上一条指令是否已经收到响应
    QWaitCondition mCmdWaitCondition;
    QLiteThread* mCmdProcessThread = nullptr;// 处理指令线程
};

#endif // COMMANDADAPTER_H
