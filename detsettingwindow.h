#ifndef DETSETTINGWINDOW_H
#define DETSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class DetSettingWindow;
}

class DetSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DetSettingWindow(QWidget *parent = nullptr);
    ~DetSettingWindow();

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端下线

    void loadAt(quint8 detId);
    void saveAt(quint8 detId);

    Q_SIGNAL void settingfinished();

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::DetSettingWindow *ui;
};

#endif // DETSETTINGWINDOW_H
