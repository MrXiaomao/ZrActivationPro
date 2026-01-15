#ifndef COUNTRATESTATISTICSWINDOW_H
#define COUNTRATESTATISTICSWINDOW_H

#include <QWidget>
#include "QGoodWindowHelper"
#include "qcustomplothelper.h"

namespace Ui {
class CountRateStatisticsWindow;
}

class QCustomPlot;
class CountRateStatisticsWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CountRateStatisticsWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~CountRateStatisticsWindow();

    void initUi();
    void initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel);
    void applyColorTheme();
    void restoreSettings();

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

private slots:
    void on_action_lightTheme_triggered();
    void on_action_darkTheme_triggered();
    void on_action_colorTheme_triggered();

    void on_action_open_triggered();

    void on_action_exit_triggered();

    void on_action_startMeasure_triggered();

    void on_action_stopMeasure_triggered();

    void on_tableWidget_cellClicked(int row, int column);

private:
    Ui::CountRateStatisticsWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;

    // 中断解析
    std::atomic<bool> mInterrupted = false;

    QMap<quint8, QVector<double>> mMapSpectrum;
    QMap<quint8, QVector<double>> mMapSpectrumAdjust;
};

#endif // COUNTRATESTATISTICSWINDOW_H
