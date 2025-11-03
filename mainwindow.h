#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "commhelper.h"
#include "clientpeerswindow.h"
#include "netsettingwindow.h"
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
    void initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int graphCount = 1);
    void applyColorTheme();
    bool openXRDFile(const QString &filePath, QVector<QPair<double, double>>& data);

public:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

    bool checkStatusTipEvent(QEvent * event);

public slots:
    void slotWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志
    void showRealCurve(const QMap<quint8, QVector<quint16>>& data);//实测曲线

signals:
    void sigUpdateBootInfo(const QString &msg);
    void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

private slots:
    void on_action_netCfg_triggered();

    void on_action_cfgParam_triggered();

    void on_action_exit_triggered();

    void on_action_open_triggered();

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

private:
    Ui::CentralWidget *ui;
    ClientPeersWindow *mClientPeersWindow = nullptr;
    NetSettingWindow *mNetSettingWindow = nullptr;
    DetSettingWindow *mDetSettingWindow = nullptr;
    bool mIsMeasuring = false;
    quint8 mCurrentPageIndex = 1;

    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    bool mIsOneLayout = false;
    QColor mThemeColor = QColor(255,255,255);

#ifdef ENABLE_MATLAB
    mwArray m_mwT;
    mwArray m_mwSeq;
    mwArray m_mwResponce_matrix;
    mwArray m_mwRom;
#endif // ENABLE_MATLAB

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
