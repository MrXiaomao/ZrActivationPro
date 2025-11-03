#include "tcpagentserver.h"

TcpAgentServer::TcpAgentServer(QObject *parent) :
    QTcpServer(parent)
{

}

//启动服务
bool TcpAgentServer::startServer(QString ip, int port)
{
    return this->listen(QHostAddress(ip), port);
}

//新的连接
void TcpAgentServer::incomingConnection(qintptr socketDescriptor)
{
    PeerConnection *connection = new PeerConnection(this);
    connection->setSocketDescriptor(socketDescriptor);
    emit connectPeerConnection(connection);
}
