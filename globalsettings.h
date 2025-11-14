#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QObject>
#include <string.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QFileInfo>
#include <QFileSystemWatcher>

// 子能谱数据包信息
#pragma pack(push, 1)  // 确保字节对齐
struct SubSpectrumPacket {
    quint32 header;        // FFFF AAB1
    quint16 dataType;      // 00D2
    quint32 spectrumSeq;   // 能谱序号
    quint32 measureTime;   // 测量时间
    quint32 deadTime;      // 死时间
    quint16 spectrumSubNo;    // 能谱编号
    quint32 spectrum[256]; // 能谱数据 256*32bit
    quint32 timeMs;        // 分秒-毫秒
    quint32 reserved1;     // 保留位 0000 0000
    quint32 reserved2;     // 保留位 0000 0000
    quint32 tail;          // FFFF CCD1
};
#pragma pack(pop)

// 完整能谱数据
#pragma pack(push, 1)  // 确保字节对齐
struct FullSpectrum {
    quint32 sequence;      // 能谱序号
    quint32 measureTime;   // 测量时间,单位ms
    quint32 deadTime;      // 死时间,单位*10ns
    quint32 spectrumData[8192]; // 8192道完整数据
    QDateTime completeTime; // 完成时间
    bool isComplete;       // 是否完整
    QSet<quint16> receivedPackets; // 已收到的包编号
};
#pragma pack(pop)

#define GLOBAL_CONFIG_FILENAME "./Config/GSettings.ini"
#define CONFIG_FILENAME "./Config/Settings.ini"
class JsonSettings : public QObject{
    Q_OBJECT
public:
    JsonSettings(const QString &fileName) {
        QFileInfo mConfigurationFile;
        mConfigurationFile.setFile(fileName);
        mFileName = mConfigurationFile.absoluteFilePath();
        mOpened = this->load();

        if (mWatchThisFile){
            // 监视文件内容变化，一旦发现变化重新读取配置文件内容，保持配置信息同步
            mConfigurationFileWatch = new QFileSystemWatcher();
            QFileInfo mConfigurationFile;
            mConfigurationFile.setFile(mFileName);
            if (!mConfigurationFileWatch->files().contains(mConfigurationFile.absoluteFilePath()))
                mConfigurationFileWatch->addPath(mConfigurationFile.absoluteFilePath());

            connect(mConfigurationFileWatch, &QFileSystemWatcher::fileChanged, this, [=](const QString &fileName){
                this->load();
            });

            //mConfigurationFileWatch->addPath(mConfigurationFile.absolutePath());//只需要监视某个文件即可，这里不需要监视整个目录
            // connect(mConfigurationFileWatch, &QFileSystemWatcher::directoryChanged, [=](const QString &path){
            // });
        }
    };
    ~JsonSettings(){
        if (mConfigurationFileWatch)
        {
            delete mConfigurationFileWatch;
            mConfigurationFileWatch = nullptr;
        }

        if (!realtime)
        {
            flush();
        }
    };

    bool isOpen() {
        return this->mOpened;
    }

    QString fileName() const{
        return mFileName;
    }

    bool flush(){
        return save(mFileName);
    };

    /*
        {
            "键key": "值value",
        }
    */
    void setValue(const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        mJsonRoot[key] = value.toJsonValue();

        if (realtime)
            flush();
    };
    // 取值
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(key);
        if (iterator != mJsonRoot.end()) {
            return mJsonRoot[key].toVariant();
        }
        else{
            return defaultValue;
        }
    };

    void setValue(QStringList &names, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValue(mJsonRoot, names, key, value);
        if (realtime)
            flush();
    };

    QVariant value(QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QReadLocker locker(&mRWLock);
        return value(mJsonRoot, names, key, defaultValue);
    };

    void setValueAt(QStringList &names, const quint8 &index, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        setValueAt(mJsonRoot, names, index, value);

        if (realtime)
            flush();
    };

    QVariant valueAt(QStringList &names, const quint8 &index, const QVariant &defaultValue){
        QReadLocker locker(&mRWLock);
        return valueAt(mJsonRoot, names, index, defaultValue);
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void appendArrayValue(const QString &arrayName, const QVariant &value)
    {
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                arrayGroup.append(value.toJsonValue());
                valueArrayRef = arrayGroup;

                if (realtime)
                    flush();
            }
        }
        else
        {
            QJsonArray arrayGroup;
            arrayGroup.append(value.toJsonValue());

            mJsonRoot.insert(arrayName, QJsonValue(arrayGroup));

            if (realtime)
                flush();
        }
    };

    /*
    {
        "arrayName":[
            "键key1": "值value1", //arrayIndex===0
            "键key2": "值value2", //arrayIndex===1
        ]
    }
    */
    void setArrayValue(const QString &arrayName, const quint8 &arrayIndex, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(arrayName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueArrayRef = iterator.value();
            if (valueArrayRef.isArray())
            {
                QJsonArray arrayGroup = valueArrayRef.toArray();
                if (arrayIndex < arrayGroup.size())
                {
                    arrayGroup.replace(arrayIndex, value.toJsonValue());
                    valueArrayRef = arrayGroup;

                    if (realtime)
                        flush();
                }
            }
        }
    };

    /*
    {
        "groupName":{
            "arrayName":[
                "键key1": "值value1", //arrayIndex===0
                "键key2": "值value2", //arrayIndex===2
            ]
        }
    }
    */

    /*
    {
        "groupName":{
            "arrayName":[
                {
                    "键key1": "值value1", //arrayIndex===0
                    "键key2": "值value2", //arrayIndex===2
                }
            ]
        }
    }
    */
    void setArrayValue(const QString &groupName, const QString &arrayName, const quint8 &arrayIndex, const QString &key, const QVariant &value){
        QWriteLocker locker(&mRWLock);
        auto iterator = mJsonRoot.find(groupName);
        if (iterator != mJsonRoot.end())
        {
            QJsonValueRef valueGroupRef = iterator.value();
            if (valueGroupRef.isObject())
            {
                QJsonObject objGroup = valueGroupRef.toObject();
                auto iterator2 = objGroup.find(arrayName);
                if (iterator2 != objGroup.end())
                {
                    QJsonValueRef valueArrayRef = iterator2.value();
                    if (valueArrayRef.isArray())
                    {
                        QJsonArray arrayGroup = valueArrayRef.toArray();
                        if (arrayIndex < arrayGroup.size()){
                            QJsonValueRef valueGroupRef = arrayGroup[arrayIndex];
                            if (valueGroupRef.isObject())
                            {
                                QJsonObject objArray = valueGroupRef.toObject();
                                objArray[key] = value.toJsonValue();

                                valueGroupRef = objArray;
                            }
                            else
                            {
                                // 找到字段，但是类型不对
                                return;
                            }
                        }
                        else
                        {
                            // 数组越界
                            QJsonObject objArray;
                            objArray[key] = value.toJsonValue();

                            arrayGroup.append(objArray);
                        }

                        valueArrayRef = arrayGroup;
                    }
                    else
                    {
                        // 找到字段，但是类型不对
                        return;
                    }
                }
                else
                {
                    QJsonObject objArray;
                    objArray[key] = value.toJsonValue();

                    QJsonArray arrayGroup;
                    arrayGroup.append(objArray);

                    objGroup.insert(arrayName, QJsonValue(arrayGroup));
                }

                valueGroupRef = objGroup;
            }
            else
            {
                // 找到字段，但是类型不对
                return;
            }
        }
        else
        {
            QJsonObject objArray;
            objArray[key] = value.toJsonValue();

            QJsonArray arrayGroup;
            arrayGroup.append(objArray);

            QJsonObject objGroup;
            objGroup.insert(arrayName, QJsonValue(arrayGroup));

            mJsonRoot.insert(groupName, QJsonValue(objGroup));
        }

        if (realtime)
            flush();
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
private:
    bool load(){
        //QReadLocker locker(&mRWLock);
        mJsonRoot = QJsonObject();
        mPrefix.clear();

        QFile file(mFileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonParseError error;
            QJsonDocument mJsonDoc = QJsonDocument::fromJson(jsonData, &error);
            if (error.error == QJsonParseError::NoError) {
                if (mJsonDoc.isObject()) {
                    mJsonRoot = mJsonDoc.object();
                    return true;
                } else {
                    qDebug() << "文件[" << mFileName << "]解析失败！";
                    return false;
                }
            } else{
                qDebug() << "文件[" << mFileName << "]解析失败！" << error.errorString().toUtf8().constData();
                return false;
            }
        } else {
            qDebug() << "文件[" << mFileName << "]打开失败！";

            //是否需要创建一个新的文件
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)){
                file.close();
                return true;
            } else {
                return false;
            }
        }
    };

    bool save(const QString &fileName = ""){
        //QWriteLocker locker(&mRWLock);
        QFile file(fileName);
        if (fileName.isEmpty())
            file.setFileName(mFileName);

        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QJsonDocument jsonDoc(mJsonRoot);
            file.write(jsonDoc.toJson());
            file.close();
            return true;
        } else {
            qDebug() << "文件[" << mFileName << "]信息保存失败！";
            return false;
        }
    };

    void setValue(QJsonObject &group, QStringList &names, const QString &key, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValue(childGroup, names, key, value);
                valueGroupRef = childGroup;
            }
            else {
                QJsonObject childGroup;
                group.insert(name, QJsonValue(childGroup));
                setValue(childGroup, names, key, value);
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                targetGroup[key] = value.toJsonValue();
                valueGroupRef = targetGroup;
            }
            else {
                QJsonObject targetGroup;
                targetGroup[key] = value.toJsonValue();
                group.insert(name, QJsonValue(targetGroup));
            }
        }
    };

    QVariant value(QJsonObject &group, QStringList &names, const QString &key, const QVariant &defaultValue = QVariant())
    {
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return value(childGroup, names, key, defaultValue);
            }
            else {
                return defaultValue;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject targetGroup = valueGroupRef.toObject();
                return targetGroup[key].toVariant();
            }
            else {
                return defaultValue;
            }
        }
    };

    void setValueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &value){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                setValueAt(childGroup, names, index, value);
                valueGroupRef = childGroup;
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray arrayGroup = valueArrayRef.toArray();
                    if (index < arrayGroup.size())
                    {
                        arrayGroup.replace(index, value.toJsonValue());
                        valueArrayRef = arrayGroup;
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

    QVariant valueAt(QJsonObject &group, QStringList &names, const quint8 &index, const QVariant &defaultValue){
        // 取首名称
        QString name = names.front();
        names.pop_front();

        if (names.size() > 0){
            auto iterator = group.find(name);
            if (iterator != group.end()) {
                QJsonValueRef valueGroupRef = iterator.value();
                QJsonObject childGroup = valueGroupRef.toObject();
                return valueAt(childGroup, names, index, defaultValue);
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        } else {
            auto iterator = group.find(name);
            if (iterator != group.end())
            {
                QJsonValueRef valueArrayRef = iterator.value();
                if (valueArrayRef.isArray())
                {
                    QJsonArray targetGroup = valueArrayRef.toArray();
                    if (index < targetGroup.size())
                    {
                        return targetGroup[index].toVariant();
                    }
                    else{
                        qDebug() << "Index out of range. " << index;
                    }
                }
                else {
                    qDebug() << "Don't find group " << name;
                }
            }
            else {
                qDebug() << "Don't find group " << name;
            }
        }
    };

protected:
    QString mFileName;
    QString mPrefix;
    QJsonObject mJsonRoot;
    QReadWriteLock mRWLock;
    QMutex mAccessMutex;//访问锁
    bool mOpened = false;//文档打开成功标识
    bool realtime = true;
    bool mWatchThisFile = false;
    QFileSystemWatcher *mConfigurationFileWatch;
};

#include <QSettings>
#include <QApplication>
class GlobalSettings: public QSettings
{
    Q_OBJECT
public:
    explicit GlobalSettings(QObject *parent = nullptr);
    explicit GlobalSettings(QString fileName, QObject *parent = nullptr);
    ~GlobalSettings();

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    void setValue(QAnyStringView key, const QVariant &value);
#else
    void setValue(const QString &key, const QVariant &value);
#endif
    void setRealtimeSave(bool realtime);
    bool isRealtimeSave() const { return realtime;}

private:
    bool realtime = false;
};

#define DET_NUM 24
#define IP_LENGTH 16
#define MAC_LENGTH 18
#define DEFAULT_HDF5_FILENAME "./Config/Settings.H5"
//参数结构定义
typedef struct _DetParameter{
    //探测器ID
    quint8 id;

    // 全局
    //心跳时间(秒)
    quint32 pluseCheckTime;
    //交换机地址
    //char switcherIp[IP_LENGTH];

    //数据接收服务器
    //IP地址
    char srvIp[IP_LENGTH];
    //子网掩码
    char srvSubnetMask[IP_LENGTH];
    //网关
    char srvGateway[IP_LENGTH];

    //时间服务器
    //IP地址
    char timerSrvIp[IP_LENGTH];

    //基本设置
    //增益
    /*
     0001：对应增益为0.08；
     0002：对应增益为0.16；
     0003：对应增益为0.32；
     0004：对应增益为0.63；
     0005：对应增益为1.26；
     0006：对应增益为2.52；
     0007：对应增益为5.01；
     0008：对应增益为10.0
    */
    quint32 gain;
    //死时间
    quint32 deathTime;
    //触发阈值
    quint32 triggerThold;

    // //网络设置
    //IP地址
    char detIp[IP_LENGTH];
    //MAC地址
    char detMacAddress[MAC_LENGTH];

    //能谱设置
    //能谱刷新时间（毫秒）
    quint32 spectrumRefreshTime;
    //能谱长度
    quint32 spectrumLength;

    //波形设置
    //触发模式
    quint8 waveformTriggerMode;
    //波形长度
    quint32 waveformLength;

    //梯形成型
    //是否启用
    bool trapShapeEnable;
    //时间常数D1
    quint32 trapShapeTimeConstD1;
    //时间常数D2
    quint32 trapShapeTimeConstD2;
    //上升沿
    quint32 trapShapeRisePoint;
    //平顶
    quint32 trapShapePeakPoint;
    //下降沿
    quint32 trapShapeFallPoint;

    //高压电源
    //是否启用
    bool highVoltageEnable;
    //DAC高压输出电平
    quint16 highVoltageOutLevel;

    _DetParameter(){
        // 全局
        //心跳时间(秒)
        pluseCheckTime = 10;
        //交换机地址
        // memset(switcherIp, 0, sizeof(switcherIp));
        // qstrcpy(switcherIp, "192.168.1.253");
        //数据接收服务器
        //IP地址
        memset(srvIp, 0, sizeof(srvIp));
        qstrcpy(srvIp, "192.168.0.200");
        //子网掩码
        memset(srvSubnetMask, 0, sizeof(srvSubnetMask));
        qstrcpy(srvSubnetMask, "255.255.255.0");
        //网关
        memset(srvGateway, 0, sizeof(srvGateway));
        qstrcpy(srvGateway, "192.168.0.1");

        //时间服务器
        //IP地址
        memset(timerSrvIp, 0, sizeof(timerSrvIp));
        qstrcpy(timerSrvIp, "192.168.0.30");

        //基本设置
        //增益
        gain = 1;
        //死时间,单位*10ns
        deathTime = 30;
        //触发阈值
        triggerThold = 100;

        //网络设置
        //IP地址
        memset(detIp, 0, sizeof(detIp));
        qstrcpy(detIp, "0.0.0.0:6000");
        //MAC地址
        memset(detMacAddress, 0, sizeof(detMacAddress));
        qstrcpy(detMacAddress, "00-08-00-00-00-00");

        //能谱设置
        //能谱刷新时间,单位ms
        spectrumRefreshTime = 1000;
        //能谱长度
        spectrumLength = 8192;

        //波形设置
        //触发模式 0-定时触发 1-普通触发
        waveformTriggerMode = 0;
        //波形长度
        waveformLength = 512;

        //梯形成型
        //是否启用
        trapShapeEnable = false;
        //时间常数D1
        trapShapeTimeConstD1 = 0;
        //时间常数D2
        trapShapeTimeConstD2 = 0;
        //上升沿
        trapShapeRisePoint = 15;
        //平顶
        trapShapePeakPoint = 15;
        //下降沿
        trapShapeFallPoint = 15;

        //高压电源
        //是否启用
        highVoltageEnable = false;
        //DAC高压输出电平
        highVoltageOutLevel = 0;
    }
}DetParameter;

#include "H5Cpp.h"
class HDF5Settings: public QObject
{
    Q_OBJECT
public:
    explicit HDF5Settings(QObject *parent = nullptr);
    ~HDF5Settings();

    static HDF5Settings *instance() {
        static HDF5Settings hdf5Settings;
        return &hdf5Settings;
    }

    QMap<quint8, DetParameter>& detParameters();
    void setDetParameter(QMap<quint8, DetParameter>&);

    void sync();

private:
    H5::CompType mCompDataType;//复合数据类型
    QMap<quint8, DetParameter> mMapDetParameter;
};

#endif // GLOBALSETTINGS_H
