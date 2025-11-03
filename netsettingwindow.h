#ifndef NETSETTINGWINDOW_H
#define NETSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class NetSettingWindow;
}

class NetSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NetSettingWindow(QWidget *parent = nullptr);
    ~NetSettingWindow();

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端下线

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::NetSettingWindow *ui;
};

#endif // NETSETTINGWINDOW_H
