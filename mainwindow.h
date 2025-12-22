#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "commhelper.h"
#include "clientpeerswindow.h"
#include "detsettingwindow.h"

#include "QGoodWindow"
#include "QGoodCentralWidget"

QT_BEGIN_NAMESPACE
namespace Ui {
class CentralWidget;
}
QT_END_NAMESPACE

class QCustomPlot;
class QCPItemText;
class QCPItemLine;
class QCPItemRect;
class QCPGraph;
class QCPAbstractPlottable;
class QCPItemCurve;

// 探测器数据结构
struct DetectorData {
    // double countRate;                    // 当前计数率
    QVector<double> countRateHistory;    // 计数率历史
    quint32 spectrum[8192];    // 累积能谱 (固定长度8192)
    // QDateTime lastUpdate;

    DetectorData(){
        for(int i=0; i<8192; i++) spectrum[i] = 0;// 初始化能谱为全0
    }
};

class CentralWidget : public QMainWindow
{
    Q_OBJECT

public:
    CentralWidget(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~CentralWidget();

    /*
    初始化
    */
    void initUi();
    void initNet();
    void restoreSettings();
    void initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, QString title, int graphCount = 1);
    void applyColorTheme();
    bool openXRDFile(const QString &filePath, QVector<QPair<double, double>>& data);

    // 新增的探测器数据管理方法
    void updateDetectorData(int detectorId, const int newSpectrum[]);
    void resetDetectorSpectrum(int detectorId);
    DetectorData getDetectorData(int detectorId) const;
    bool isDetectorOnline(int detectorId) const;
    QList<int> getOnlineDetectors() const;

    // 更新能谱显示
    void updateSpectrumDisplay(int detectorId, const quint32 spectrum[]);

    // 添加计数率显示更新函数
    void updateCountRateDisplay(int detectorId, double countRate);

    //展示当前页面的六个能谱图
    void showSpectrumDisplay(int currentPageIndex);

    //展示当前页面的六个计数率图
    void showCountRateDisplay(int currentPageIndex);

private:
    //最近窗口的 min/max
    static inline QPair<double,double> calcRecentYRange(const QVector<double>& history, int windowPoints)
    {
        if (history.isEmpty())
            return {0.0, 1.0};

        int n = history.size();
        int start = qMax(0, n - windowPoints);

        double ymin = history[start];
        double ymax = history[start];
        for (int i = start + 1; i < n; ++i) {
            double v = history[i];
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
        if (qFuzzyCompare(ymin, ymax)) {
            ymax = ymin + 1.0; // 防止上下界相等导致范围为0
        }
        return {ymin, ymax};
    }

public:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

    bool checkStatusTipEvent(QEvent * event);

public slots:
    void slotWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志

signals:
    void sigUpdateBootInfo(const QString &msg);
    void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void detectorDataUpdated(int detectorId, double countRate, const int spectrum[]);

private slots:
    void on_action_cfgParam_triggered();

    void on_action_exit_triggered();

    void on_action_open_triggered();

    void onTriggerModelChanged(int index);

    void on_action_startMeasure_triggered();

    void on_action_stopMeasure_triggered();

    void on_action_powerOn_triggered();

    void on_action_powerOff_triggered();

    void on_action_startServer_triggered();

    void on_action_stopServer_triggered();

    void on_action_about_triggered();

    void on_action_aboutQt_triggered();

    void on_action_lightTheme_triggered();

    void on_action_darkTheme_triggered();

    void on_action_colorTheme_triggered();

    void on_pushButton_stopMeasureDistance_clicked();

    void on_pushButton_startMeasureDistance_clicked();

    void on_pushButton_saveAs_clicked();

    void on_action_localService_triggered();

    void on_pushButton_startMeasure_clicked();

    void on_pushButton_stopMeasure_clicked();

    void on_action_connect_triggered();

private:
    Ui::CentralWidget *ui;
    ClientPeersWindow *mClientPeersWindow = nullptr;
    DetSettingWindow *mDetSettingWindow = nullptr;
    bool mIsMeasuring = false;
    quint8 mCurrentPageIndex = 1;
    QMutex mMutexSwitchPage;

    QVector<quint8> m_selectedChannels; // 记录所选的通道号，停止测量时使用(自定义通道测量)
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    bool mIsOneLayout = false;
    QColor mThemeColor = QColor(255,255,255);

    //记录温度超时报警的探测器ID
    QVector<quint8> mTemperatureTimeoutDetectors;
    //记录探测器是否正在测量
    QMap<quint8, bool> mDetectorMeasuring;
    // 探测器数据管理
    QHash<int, DetectorData> m_detectorData;  // 只存储联网探测器的数据
    CommHelper *commHelper = nullptr;
    class MainWindow *mainWindow = nullptr;

    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆
};

class MainWindow : public QGoodWindow
{
    Q_OBJECT
public:
    explicit MainWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~MainWindow();
    void fixMenuBarWidth(void) {
        if (mMenuBar) {
            /* FIXME: Fix the width of the menu bar
             * please optimize this code */
            int width = 0;
            int itemSpacingPx = mMenuBar->style()->pixelMetric(QStyle::PM_MenuBarItemSpacing);
            for (int i = 0; i < mMenuBar->actions().size(); i++) {
                QString text = mMenuBar->actions().at(i)->text();
                QFontMetrics fm(mMenuBar->font());
                width += fm.size(0, text).width() + itemSpacingPx*1.5;
            }
            mGoodCentraWidget->setLeftTitleBarWidth(width);
        }
    }

    CentralWidget* centralWidget() const
    {
        return this->mCentralWidget;
    }


protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent * event) override;

private:
    QGoodCentralWidget *mGoodCentraWidget;
    QMenuBar *mMenuBar = nullptr;
    CentralWidget *mCentralWidget;
};

#endif // MAINWINDOW_H
