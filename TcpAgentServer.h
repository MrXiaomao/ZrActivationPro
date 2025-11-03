#ifndef REGISTERSERVER_H
#define REGISTERSERVER_H

#include "PeerConnection.h"
#include <QTcpServer>

class TcpAgentServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit TcpAgentServer(QObject *parent = 0);

    bool startServer(QString ip, int port);

signals:
    void connectPeerConnection(PeerConnection *connection);

protected:
    // 新的连接
    void incomingConnection(qintptr socketDescriptor);

private:
    QHostAddress myIP;
    int myPort;
};

#endif // REGISTERSERVER_H
