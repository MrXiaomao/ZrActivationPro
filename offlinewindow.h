#ifndef OFFLINEWINDOW_H
#define OFFLINEWINDOW_H

#include <QWidget>
#include "QGoodWindowHelper"

namespace Ui {
class OfflineWindow;
}

class OfflineWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit OfflineWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~OfflineWindow();

    void initCustomPlot();

private slots:
    void on_pushButton_start_clicked();
    void on_pushbutton_save_clicked();


private:
    Ui::OfflineWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;

    void applyColorTheme();
};

#endif // OFFLINEWINDOW_H
