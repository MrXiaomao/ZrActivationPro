/* 说明：
 * RegisterConnection用于与中心验证服务器通信，主要交换Register注册信息和PeerList活跃用户信息
 * 这里，我们也有几个设计说明：
 * (1) 我们仍然设置Greeting的过程用于初始的连接建立过程
 * (2) 我们设置周期定时器用于周期发送Register信息和获取最新的活跃用户信息
 *
 */

#ifndef REGISTERCONNECTION_H
#define REGISTERCONNECTION_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class TcpConnection : public QTcpSocket
{
    Q_OBJECT
public:
    explicit TcpConnection(QObject *parent = 0);
    virtual void disconnectFromHost();

signals:
    void ackLoginResult(bool result);
    void ackRegistResult(bool result);
    void ackUpdateResult(bool result);
    void ackFindResult(bool result);

    void socketReceived(TcpConnection *, QByteArray);
    void socketError(TcpConnection *, QAbstractSocket::SocketError);

public slots:
    void handleSocketConnected();
    void handleSocketReceived();
    void handleSocketError(QAbstractSocket::SocketError error);

    void handleTimer();

public:
    //连接标志
    bool isConnected;
    QString account;
    qint64 send(const QByteArray &data);

private:
    //包缓冲区
    QByteArray buffer;
    QByteArray pool;//缓冲池
    qint32  pksize;//包大小

    //重连时钟
    QTimer reConnectTimer;
    QString serverIp;
    qint64 serverPort;
    bool enableReconnectTimer;

public:
    //连服务器
    bool initNetwork(QString srvIP = "0.0.0.0", uint srvPort = 8000);
    void enableReconectTimer(bool enable = true);
};

#endif // REGISTERCONNECTION_H
