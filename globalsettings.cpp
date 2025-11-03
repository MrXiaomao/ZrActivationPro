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

void GlobalSettings::setRealtimeSave(bool realtime)
{
    this->realtime = realtime;
}

//////////////////////////////
HDF5Settings::HDF5Settings(QObject *parent) : QObject(parent)
{
    //初始化数据类型
    mCompDataType = H5::CompType(sizeof(DetParameter));//复合数据类型

    //H5::StrType ipStrType(H5::PredType::C_S1, H5T_VARIABLE);
    // 定义固定长度字符串类型(20字符)
    H5::StrType ipStrType(H5::PredType::C_S1, IP_LENGTH);
    H5::StrType macStrType(H5::PredType::C_S1, MAC_LENGTH);

    mCompDataType.insertMember("id", HOFFSET(DetParameter, id), H5::PredType::NATIVE_UINT8);

    // 全局
    {
        //心跳时间(秒)
        mCompDataType.insertMember("pluseCheckTime", HOFFSET(DetParameter, pluseCheckTime), H5::PredType::NATIVE_INT);
        //交换机地址
        //mCompDataType.insertMember("switcherIp", HOFFSET(DetParameter, switcherIp), ipStrType);
        //数据接收服务器
        //IP地址
        mCompDataType.insertMember("srvIp", HOFFSET(DetParameter, srvIp), ipStrType);
        //子网掩码
        mCompDataType.insertMember("srvSubnetMask", HOFFSET(DetParameter, srvSubnetMask), ipStrType);
        //网关
        mCompDataType.insertMember("srvGateway", HOFFSET(DetParameter, srvGateway), ipStrType);
        //时间服务器
        //IP地址
        mCompDataType.insertMember("timerSrvIp", HOFFSET(DetParameter, timerSrvIp), ipStrType);
    }

    //基本设置
    {
        //增益
        mCompDataType.insertMember("gain", HOFFSET(DetParameter, gain), H5::PredType::NATIVE_ULONG);
        //死时间
        mCompDataType.insertMember("deathTime", HOFFSET(DetParameter, deathTime), H5::PredType::NATIVE_ULONG);
        //触发阈值
        mCompDataType.insertMember("triggerThold", HOFFSET(DetParameter, triggerThold), H5::PredType::NATIVE_ULONG);
    }

    //网络设置
    {
        //IP地址
        char detIp[16];
        mCompDataType.insertMember("detIp", HOFFSET(DetParameter, detIp), ipStrType);
        //MAC地址
        mCompDataType.insertMember("detMacAddress", HOFFSET(DetParameter, detMacAddress), macStrType);
    }

    //能谱设置
    {
        //能谱刷新时间（毫秒）
        mCompDataType.insertMember("spectrumRefreshTime", HOFFSET(DetParameter, spectrumRefreshTime), H5::PredType::NATIVE_ULONG);
        //能谱长度
        mCompDataType.insertMember("spectrumLength", HOFFSET(DetParameter, spectrumLength), H5::PredType::NATIVE_ULONG);
    }

    //波形设置
    {
        //触发模式
        mCompDataType.insertMember("waveformTriggerMode", HOFFSET(DetParameter, waveformTriggerMode), H5::PredType::NATIVE_UINT8);
        //波形长度
        mCompDataType.insertMember("waveformLength", HOFFSET(DetParameter, waveformLength), H5::PredType::NATIVE_ULONG);
    }

    //梯形成型
    {
        //是否启用

        mCompDataType.insertMember("trapShapeEnable", HOFFSET(DetParameter, trapShapeEnable), H5::PredType::NATIVE_HBOOL);
        //时间常数D1
        mCompDataType.insertMember("trapShapeTimeConstD1", HOFFSET(DetParameter, trapShapeTimeConstD1), H5::PredType::NATIVE_ULONG);
        //时间常数D2
        mCompDataType.insertMember("trapShapeTimeConstD2", HOFFSET(DetParameter, trapShapeTimeConstD2), H5::PredType::NATIVE_ULONG);
        //上升沿
        mCompDataType.insertMember("trapShapeRisePoint", HOFFSET(DetParameter, trapShapeRisePoint), H5::PredType::NATIVE_ULONG);
        //平顶
        mCompDataType.insertMember("trapShapePeakPoint", HOFFSET(DetParameter, trapShapePeakPoint), H5::PredType::NATIVE_ULONG);
        //下降沿
        mCompDataType.insertMember("trapShapeFallPoint", HOFFSET(DetParameter, trapShapeFallPoint), H5::PredType::NATIVE_ULONG);
    }

    //高压电源
    {
        //是否启用
        mCompDataType.insertMember("highVoltageEnable", HOFFSET(DetParameter, highVoltageEnable), H5::PredType::NATIVE_HBOOL);
        //DAC高压输出电平
        mCompDataType.insertMember("highVoltageOutLevel", HOFFSET(DetParameter, highVoltageOutLevel), H5::PredType::NATIVE_USHORT);
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
