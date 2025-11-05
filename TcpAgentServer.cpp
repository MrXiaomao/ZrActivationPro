#include "tcpagentserver.h"

TcpAgentServer::TcpAgentServer(QObject *parent) :
    QTcpServer(parent)
{

}

void TcpAgentServer::incomingConnection(qintptr socketDescriptor)
{
    emit newConnection(socketDescriptor);
}
