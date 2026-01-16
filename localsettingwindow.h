#ifndef LOCALSETTINGWINDOW_H
#define LOCALSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class LocalSettingWindow;
}

class LocalSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LocalSettingWindow(QWidget *parent = nullptr);
    ~LocalSettingWindow();

    Q_SLOT void updateData();

private slots:
    void on_pushButton_ok_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::LocalSettingWindow *ui;
};

#endif // LOCALSETTINGWINDOW_H
