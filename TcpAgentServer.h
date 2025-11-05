#ifndef REGISTERSERVER_H
#define REGISTERSERVER_H

//#include "PeerConnection.h"
#include <QTcpServer>

class TcpAgentServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit TcpAgentServer(QObject *parent = 0);

    Q_SIGNAL void newConnection(qintptr socketDescriptor);

protected:
    virtual void incomingConnection(qintptr socketDescriptor) override;

private:
    QHostAddress myIP;
    int myPort;
};

#endif // REGISTERSERVER_H
