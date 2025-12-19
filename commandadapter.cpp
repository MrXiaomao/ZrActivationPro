#include "commandadapter.h"
#include <QDebug>
#include <QtEndian>

CommandAdapter::CommandAdapter(QObject *parent)
    : QObject{parent}
{
    mCmdProcessThread = new QLiteThread(this);
    //mCmdProcessThread->setObjectName("mCmdProcessThread");
    mCmdProcessThread->setWorkThreadProc([=](){
        qRegisterMetaType<QByteArray>("QByteArray&");
        //需要参数类型已注册为元类型
        qRegisterMetaType<QVector<quint32>>("QVector<quint32>");//用于传输能谱
        qRegisterMetaType<quint32>("quint32");      //用于传输探测器通道号

        while (!mTerminatedThread)
        {
            {
                QMutexLocker locker(&mCmdMutex);
                while (!mCmdReady){
                    mCmdWaitCondition.wait(&mCmdMutex);
                }

                if (mCmdQueue.size() > 0){
                    QByteArray askCmd = mCmdQueue.dequeue();
                    sendCmdToSocket(askCmd);
                }
                mCmdReady = false;
            }

            QThread::msleep(1);
        }
    });
    mCmdProcessThread->start();
    connect(this, &CommandAdapter::destroyed, [=]() {
        mCmdProcessThread->exit(0);
        mCmdProcessThread->wait(500);
    });
}

CommandAdapter::~CommandAdapter()
{
    mTerminatedThread = true;
    mCmdReady = true;
    mCmdWaitCondition.wakeAll();
    mCmdProcessThread->wait();
}

/*
 * 发送下一条指令
*/
void CommandAdapter::pushCmd(QByteArray& askCmd)
{
    QMutexLocker locket(&mCmdMutex);
    //mCmdQueue.enqueue(askCmd);//因为指令没有返回码，所以没必要加入堆栈
    sendCmdToSocket(askCmd);
}

/*
 * 发送下一条指令
*/
void CommandAdapter::notifySendNextCmd()
{
    mCmdReady = true;
    mCmdWaitCondition.wakeAll();
}

/*
     清空指令
    */
void CommandAdapter::clear()
{
    QMutexLocker locket(&mCmdMutex);
    mCmdQueue.clear();
}

void CommandAdapter::analyzeCommands(QByteArray &cachePool)
{
    while(1){
        //判断缓存数据大小是否小于指令集长度最小单位
        if (cachePool.size() < 12)
            break;

        bool findNaul = false;
        //增益指令
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 10"))){
                qInfo().noquote() << "增益：" << cachePool.at(9);

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //死时间配置(ns)
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 11"))){
                qInfo().noquote() << "死时间/ns：" << cachePool.mid(8, 2).toShort();

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //触发阈值
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 12"))){
                qInfo().noquote() << "触发阈值：" << cachePool.mid(8, 2).toShort();

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         波形基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FC 10"))){
                qInfo().noquote() << "触发模式：" << (cachePool.mid(7, 1).toShort() == tmTimer ? "定时触发" : "正常触发模式");
                WaveformLength waveformLength = (WaveformLength)cachePool.mid(9, 1).toShort();
                if (waveformLength == wl64)
                    qInfo().noquote() << "波形长度：64";
                else if (waveformLength == wl128)
                    qInfo().noquote() << "波形长度：128";
                else if (waveformLength == wl256)
                    qInfo().noquote() << "波形长度：256";
                else if (waveformLength == wl512)
                    qInfo().noquote() << "波形长度：512";
                else
                    qInfo().noquote() << "波形长度：未知值";

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         能谱基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FD 10"))){
                qInfo().noquote() << "能谱刷新时间/ms：" << cachePool.mid(6, 4).toUInt();

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         梯形成型基本配置
        ***********************************************************/
        //梯形成型时间常数配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 10"))){
                quint16 d1 = cachePool.mid(6, 2).toShort();
                quint16 d2 = cachePool.mid(8, 9).toShort();
                float f1 = (float)d1 / 65536;
                float f2 = (float)d2 / 65536;
                qInfo().noquote() << "梯形成型时间常数，d1=" << f1 << "，d2="<<f2;

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //上升沿、平顶、下降沿长度配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 11"))){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "上升沿=" << rise << "，平顶="<<peak<< "，下降沿="<<fall;

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //梯形成型使能配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 12"))){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "梯形成型使能状态：" << ((quint8)cachePool.at(9) == 0x00 ? "关闭" : "打开");

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         工作模式配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FF 10"))){
                WorkMode workMode = (WorkMode)cachePool.at(9);
                if (workMode == wmWaveform)
                    qInfo().noquote() << "工作模式：波形模式";
                else if (workMode == wmSpectrum)
                    qInfo().noquote() << "工作模式：能谱模式";
                else if (workMode == wmParticle)
                    qInfo().noquote() << "工作模式：粒子模式";
                else
                        qInfo().noquote() << "工作模式：未知";

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         高压电源配置
        ***********************************************************/
        //高压使能配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 10"))){
                HighVolgateOutLevelEnable highVoltageEnable = (HighVolgateOutLevelEnable)cachePool.at(9);
                qInfo().noquote() << "高压使能状态：" << (highVoltageEnable ? "关闭" : "打开");

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //DAC输出电平配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 11"))){
                quint16 level = cachePool.mid(8, 2).toShort();
                qInfo().noquote() << "DAC输出电平值：" << level;

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         控制类指令
        ***********************************************************/
        //开始测量
        /*if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0F EA 10 00 00 00 01 AB CD"))){
            qInfo().noquote() << "下发指令返回：开始测量";

            mIsMeasuring = true;
            findNaul = true;
            cachePool.remove(0, 12);

            //上报开始测量状态
            QMetaObject::invokeMethod(this, "reportStartMeasure", Qt::QueuedConnection);
        }

        //停止测量
        if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0F EA 10 00 00 00 00 AB CD"))){
            qInfo().noquote() << "下发指令返回：停止测量";

            mIsMeasuring = false;
            findNaul = true;
            cachePool.remove(0, 12);

            //上报结束测量状态
            QMetaObject::invokeMethod(this, "reportStopMeasure", Qt::QueuedConnection);
        }*/

        /*********************************************************
         应答类指令
        ***********************************************************/
        //程序版本号查询
        if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A DA 10"))){
            //硬件版本号
            QString hardVersion = QString("%1.%2.%3").arg(cachePool.at(6)).arg(cachePool.at(7)).arg(cachePool.at(8));
            //测试版本标志位
            bool isTest = cachePool.at(9) == 0x01;

            qInfo().noquote() << "硬件版本号：" << hardVersion;
            qInfo().noquote() << "是否测试版本：" << (isTest ? "是" : "否");

            findNaul = true;
            cachePool.remove(0, 12);
        }

        //温度监测（心跳检测）
        if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A DA 11"))){
            QByteArray data = cachePool.mid(6, 4);
            qint32 t = qFromBigEndian<qint32>(data.constData());
            float temperature = t * 0.0001;// 换算系数

            qDebug().noquote() << "温度：" << temperature;

            findNaul = true;
            cachePool.remove(0, 12);
            
            //上传温度数据
            QMetaObject::invokeMethod(this, "reportTemperatureData", Qt::QueuedConnection, Q_ARG(float, temperature));
            // emit reportTemperatureData(temperature);
            //发送心跳包，作为反馈
            // this->sendPluse();
        }

        /*********************************************************
         OTA更新指令
        ***********************************************************/
        //Flash地址设置
        //OTA更新数据传输指令

        //重加载FPGA程序
        if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0F CA 12"))){
            qInfo().noquote() << "下发指令返回：停止测量";

            findNaul = true;
            cachePool.remove(0, 12);

            //上报重加载FPGA程序状态
            QMetaObject::invokeMethod(this, "reportRetHostProgramSuccess", Qt::QueuedConnection);
        }

        if (findNaul){
            notifySendNextCmd();
            findNaul = false;
        }

        if (cachePool.startsWith(QByteArray::fromHex("FF FF AA B1"))){
            findNaul = true;

            //有效数据包长度
            quint32 onePkgSize = 0;

            //数据类型
            bool ok;
            DataType dataType = (DataType)cachePool.mid(4, 2).toHex().toUShort(&ok, 16);
            if (dataType == dtWaveform){
                //波形
                //包头0xFFFFAAB1 + 数据类型（0x00D1）+ 波形数据（波形长度*16bit） + 保留位（32bit）+ 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 512*2 + 4 + 4;
            }
            else if (dataType == dtSpectrum){
                //能谱
                //包头0xFFFFAAB1 + 数据类型（0x00D2）+ 能谱序号（32bit） + 测量时间（32bit） + 死时间（32bit）+ 能谱编号（16bit）+ 能谱数据（256*32bit）+分秒-毫秒（32bit）+ 保留位（64bit） + 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 4 + 4 + 4 + 2 + 256*4 + 4 + 8 + 4;
            }
            else{
                /*异常数据，一定要注意！！！！！！！！！！！！！！！！！*/
                findNaul = false;

                qInfo() << "数据包类型错误：" << cachePool.mid(4, 2).toHex(' ');

                //重新开始寻找包头
                cachePool.remove(0, 4);
            }

            if (cachePool.size() >= onePkgSize){
                // 满足数据包长度
                QByteArray chunk = cachePool.left(onePkgSize);

                //继续检查包尾
                if (chunk.endsWith(QByteArray::fromHex(QString("FF FF CC D1").toUtf8()))){
                    mValidDataPkgRef++;
                    cachePool.remove(0, onePkgSize);

                    if (dataType == dtWaveform)
                        QMetaObject::invokeMethod(this, "reportWaveformData", Qt::QueuedConnection, Q_ARG(QByteArray&, chunk));
                    else if (dataType == dtSpectrum)
                        QMetaObject::invokeMethod(this, "reportSpectrumData", Qt::QueuedConnection, Q_ARG(QByteArray&, chunk));
                    else if (dataType == dtParticle)
                        QMetaObject::invokeMethod(this, "reportParticleData", Qt::QueuedConnection, Q_ARG(QByteArray&, chunk));

                    //上报有效数据包个数
                    // QMetaObject::invokeMethod(this, "reportValidDataPkgRef", Qt::QueuedConnection, Q_ARG(quint32, mValidDataPkgRef));
                }
                else {
                    /*异常数据，一定要注意！！！！！！！！！！！！！！！！！*/
                    findNaul = false;                

                    // 包头/包尾不对 重新开始寻找包头
                    cachePool.remove(0, 4);
                    continue;
                }
            }
            else{
                //波形数据不足
                break;
            }
        }

        if (!findNaul && cachePool.size()>12){
            /*包头/包尾不对*/
            // qDebug() << "Invalid2: " << cachePool.left(4).toHex(' ');

            /*继续寻找包头,删除包头继续寻找*/
            cachePool.remove(0, 1);
            break;
        }
    }
}
