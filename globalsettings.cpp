#include "globalsettings.h"
#include <QFileInfo>
#include <QApplication>
#include <QTextCodec>

/*#########################################################*/
GlobalSettings::GlobalSettings(QObject *parent)
    : QSettings(GLOBAL_CONFIG_FILENAME, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::GlobalSettings(QString filePath, QObject *parent)
    : QSettings(filePath, QSettings::IniFormat, parent)
{
    this->setIniCodec(QTextCodec::codecForName("utf-8"));
}

GlobalSettings::~GlobalSettings() {

}

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
void GlobalSettings::setValue(QAnyStringView key, const QVariant &value)
#else
void GlobalSettings::setValue(const QString &key, const QVariant &value)
#endif
{
    QSettings::setValue(key, value);
    if(realtime)
        sync();
}

void GlobalSettings::setDoubleVector(const QString& key, const QVector<double>& vec)
{
    QStringList sl;
    sl.reserve(vec.size());

    // 'g', 17：尽量保证 double 往返不丢精度
    for (double v : vec)
        sl << QString::number(v, 'g', 17);

    setValue(key, sl);
    // 如果你们希望立刻落盘，可选sync()：
    if(realtime)
        sync();
}

// 从配置读取 double 向量
// eg：QVector<double> loaded = settings.doubleVector("Global/Keys");
QVector<double> GlobalSettings::GetDoubleVector(const QString& key,
                                            const QVector<double>& def) const
{
    const QVariant v = value(key);
    if (!v.isValid())
        return def;

    // Qt/ini 里通常会以 QStringList 形式读回
    const QStringList sl = v.toStringList();
    if (sl.isEmpty())
        return QVector<double>{}; // 或 def，看你语义

    QVector<double> vec;
    vec.reserve(sl.size());
    for (const QString& s : sl)
        vec << s.toDouble();

    return vec;
}

void GlobalSettings::setRealtimeSave(bool realtime)
{
    this->realtime = realtime;
}

//////////////////////////////
HDF5Settings::HDF5Settings(QObject *parent) : QObject(parent)
{
    //初始化数据类型
    mCompDataType = H5::CompType(sizeof(DetParameter));//复合数据类型

    // 定义固定长度字符串类型(20字符)
    H5::StrType ipStrType(H5::PredType::C_S1, IP_LENGTH);

    mCompDataType.insertMember("id", HOFFSET(DetParameter, id), H5::PredType::NATIVE_UINT8);

    // 全局
    {
        //心跳时间(秒)
        mCompDataType.insertMember("checkTime", HOFFSET(DetParameter, pluseCheckTime), H5::PredType::NATIVE_INT);
        //交换机地址
        //mCompDataType.insertMember("switcherIp", HOFFSET(DetParameter, switcherIp), ipStrType);

        //时间服务器
        //IP地址
        mCompDataType.insertMember("timerSrvIp", HOFFSET(DetParameter, timerSrvIp), ipStrType);
    }

    //基本设置
    {
        //增益
        mCompDataType.insertMember("gain", HOFFSET(DetParameter, gain), H5::PredType::NATIVE_DOUBLE);
        //死时间
        mCompDataType.insertMember("deathT", HOFFSET(DetParameter, deathTime), H5::PredType::NATIVE_UINT8);
        //触发阈值
        mCompDataType.insertMember("Threshold", HOFFSET(DetParameter, triggerThold), H5::PredType::NATIVE_UINT16);
    }

    // 探测器网络设置，用于界面匹配对应通道
    {
        //IP地址,port号
        char det_Ip_port[16];
        mCompDataType.insertMember("detIpPort", HOFFSET(DetParameter, det_Ip_port), ipStrType);
    }

    //能谱设置
    {
        //能谱刷新时间（毫秒）
        mCompDataType.insertMember("specDeltaT", HOFFSET(DetParameter, spectrumRefreshTime), H5::PredType::NATIVE_ULONG);
        //能谱长度
        mCompDataType.insertMember("specLen", HOFFSET(DetParameter, spectrumLength), H5::PredType::NATIVE_ULONG);
    }

    //波形设置
    {
        //触发模式
        mCompDataType.insertMember("waveTrigMode", HOFFSET(DetParameter, waveformTriggerMode), H5::PredType::NATIVE_UINT8);
        //波形长度
        mCompDataType.insertMember("waveLen", HOFFSET(DetParameter, waveformLength), H5::PredType::NATIVE_ULONG);
    }

    //梯形成型
    {
        //是否启用

        mCompDataType.insertMember("TShapeEnable", HOFFSET(DetParameter, trapShapeEnable), H5::PredType::NATIVE_HBOOL);
        //时间常数D1
        mCompDataType.insertMember("TShapeD1", HOFFSET(DetParameter, trapShapeTimeConstD1), H5::PredType::NATIVE_UINT16);
        //时间常数D2
        mCompDataType.insertMember("TShapeD2", HOFFSET(DetParameter, trapShapeTimeConstD2), H5::PredType::NATIVE_UINT16);
        //上升沿
        mCompDataType.insertMember("TShapeRise", HOFFSET(DetParameter, trapShapeRisePoint), H5::PredType::NATIVE_UINT8);
        //平顶
        mCompDataType.insertMember("TShapePeak", HOFFSET(DetParameter, trapShapePeakPoint), H5::PredType::NATIVE_UINT8);
        //下降沿
        mCompDataType.insertMember("TShapeFall", HOFFSET(DetParameter, trapShapeFallPoint), H5::PredType::NATIVE_UINT8);
    }

    //高压电源
    {
        //是否启用
        mCompDataType.insertMember("HV_Enable", HOFFSET(DetParameter, highVoltageEnable), H5::PredType::NATIVE_HBOOL);
        //DAC高压输出电平
        mCompDataType.insertMember("HV_Out", HOFFSET(DetParameter, highVoltageOutLevel), H5::PredType::NATIVE_USHORT);
    }

    if (QFileInfo::exists(DEFAULT_HDF5_FILENAME)){
        H5::H5File *pfH5Setting = new H5::H5File(DEFAULT_HDF5_FILENAME, H5F_ACC_RDONLY);
        H5::Group cfgGroup = pfH5Setting->openGroup("Config");
        H5::DataSet globalDataset = cfgGroup.openDataSet("Detector");
        H5::DataSpace file_space = globalDataset.getSpace();

        // 获取数据集维度(行)
        int ndims = file_space.getSimpleExtentNdims();
        std::vector<hsize_t> dims(ndims);
        file_space.getSimpleExtentDims(dims.data());

        // 读取数据
        //一次性读取所有数据
        QVector<DetParameter> read_data(dims[0]);
        globalDataset.read(read_data.data(), mCompDataType);
        for (const auto& item : read_data) {
            mMapDetParameter[item.id] = item;
        }

        //逐行读取数据
        // size_t chunkSize = 1;//一次读1行数据
        // for (int i=0; i<dims[0]; ++i){
        //     hsize_t offset[1] = {(hsize_t)i};
        //     hsize_t chunkDims[1] = {chunkSize};

        //     // 准备内存空间
        //     H5::DataSpace mem_space(1, chunkDims);

        //     // 文件定位
        //     file_space.selectHyperslab(H5S_SELECT_SET, chunkDims, offset);

        //     QVector<DetParameter> chunkData(chunkSize);
        //     globalDataset.read(chunkData.data(), mCompDataType, mem_space, file_space);
        //     for (const auto& item : chunkData) {
        //         mMapDetParameter[item.id] = item;
        //     }
        // }

        pfH5Setting->close();
        delete pfH5Setting;
    }
    else{
        for (int i=1; i<=DET_NUM; ++i){
            DetParameter detParameter;
            detParameter.id = i;
            mMapDetParameter[i] = detParameter;
        }

        try {
            H5::H5File fH5Setting(DEFAULT_HDF5_FILENAME, H5F_ACC_TRUNC);

            QVector<DetParameter> data;
            for (auto &pair : mMapDetParameter.toStdMap())
            {
                data.push_back(pair.second);
            }

            hsize_t dims[1] = {DET_NUM};
            H5::DataSpace dataspace(1, dims);
            H5::Group cfgGroup = fH5Setting.createGroup("Config");
            H5::DataSet dataset = cfgGroup.createDataSet("Detector", mCompDataType, dataspace);


            dataset.write(data.data(), mCompDataType);
            fH5Setting.close();
        } catch (H5::FileIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSetIException& error) {
            error.printErrorStack();
            return ;
        } catch (H5::DataSpaceIException& error) {
            error.printErrorStack();
            return ;
        }
    }
}

HDF5Settings::~HDF5Settings()
{

}

QMap<quint8, DetParameter>& HDF5Settings::detParameters()
{
    return mMapDetParameter;
}

void HDF5Settings::setDetParameter(QMap<quint8, DetParameter>& detParameters)
{
    mMapDetParameter = detParameters;
}

void HDF5Settings::sync()
{
    try {
        H5::H5File fH5Setting(DEFAULT_HDF5_FILENAME, H5F_ACC_TRUNC);

        QVector<DetParameter> data;
        for (auto &pair : mMapDetParameter.toStdMap())
        {
            data.push_back(pair.second);
        }

        hsize_t dims[1] = {DET_NUM};
        H5::DataSpace dataspace(1, dims);
        H5::Group cfgGroup = fH5Setting.createGroup("Config");
        H5::DataSet dataset = cfgGroup.createDataSet("Detector", mCompDataType, dataspace);
        writeDetParStrAttr(dataset);
        dataset.write(data.data(), mCompDataType);
        fH5Setting.close();
    } catch (H5::FileIException& error) {
        error.printErrorStack();
        return ;
    } catch (H5::DataSetIException& error) {
        error.printErrorStack();
        return ;
    } catch (H5::DataSpaceIException& error) {
        error.printErrorStack();
        return ;
    }
}

void HDF5Settings::writeDetParStrAttr(H5::DataSet& dataset)
{
    // 给每个字段写解释（多个键）
    writeStrAttr(dataset, "id",   "通道号(0~23)");
    writeStrAttr(dataset, "checkTime", "心跳时间(秒)");
    writeStrAttr(dataset, "timerSrvIp", "授时服务器 IP 地址");
    writeStrAttr(dataset, "gain", "增益(0.08~10.0)");
    writeStrAttr(dataset, "deathT", "探测器死时间设定(*10ns)");
    writeStrAttr(dataset, "Threshold", "触发阈值，低于阈值的波形不计入能谱");
    writeStrAttr(dataset, "detIpPort", "探测器 IP 地址和端口号");
    writeStrAttr(dataset, "specDeltaT", "能谱刷新时间(毫秒)");
    writeStrAttr(dataset, "specLen", "能谱长度");
    writeStrAttr(dataset, "waveTrigMode", "波形触发模式(0-定时触发 1-普通触发)");
    writeStrAttr(dataset, "waveLen", "波形长度");
    writeStrAttr(dataset, "TShapeEnable", "梯形成型使能(0/1)");
    writeStrAttr(dataset, "TShapeD1", "梯形成型时间常数 D1");
    writeStrAttr(dataset, "TShapeD2", "梯形成型时间常数 D2");
    writeStrAttr(dataset, "TShapeRise", "梯形成型上升沿点数");
    writeStrAttr(dataset, "TShapePeak", "梯形成型平顶点数");
    writeStrAttr(dataset, "TShapeFall", "梯形成型下降沿点数");
    writeStrAttr(dataset, "HV_Enable", "高压使能(0/1)");
    writeStrAttr(dataset, "HV_Out", "DAC 高压输出电平(0~4095)");
}

// 写一个标量字符串 attribute（可变长）
void HDF5Settings::writeStrAttr(H5::DataSet& ds, const std::string& key, const std::string& val) {
    H5::StrType vstr(H5::PredType::C_S1, H5T_VARIABLE);
    H5::DataSpace scalar(H5S_SCALAR);

    H5::Attribute a = ds.createAttribute(key, vstr, scalar);
    // H5Cpp 常见写法：写 const char*
    const char* s = val.c_str();
    a.write(vstr, &s);
}
