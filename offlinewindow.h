#ifndef OFFLINEWINDOW_H
#define OFFLINEWINDOW_H

#include <QWidget>
#include "QGoodWindowHelper"
#include "qcustomplothelper.h"

namespace Ui {
class OfflineWindow;
}

class QCustomPlot;
class OfflineWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit OfflineWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~OfflineWindow();

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

private:
    Ui::OfflineWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;

    // 中断解析
    std::atomic<bool> mInterrupted = false;

    QMap<quint8, QVector<double>> mMapSpectrum;
    QMap<quint8, QVector<double>> mMapSpectrumAdjust;
};

#endif // OFFLINEWINDOW_H
