#ifndef CLIENTPEERSWINDOW_H
#define CLIENTPEERSWINDOW_H

#include <QWidget>

namespace Ui {
class ClientPeersWindow;
}

class ClientPeersWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ClientPeersWindow(QWidget *parent = nullptr);
    ~ClientPeersWindow();

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端下线

private slots:
    void on_pushButton_clicked();

private:
    Ui::ClientPeersWindow *ui;
};

#endif // CLIENTPEERSWINDOW_H
