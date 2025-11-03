#include "PeerConnection.h"
#include <QDataStream>
#include <QHostAddress>

PeerConnection::PeerConnection(QObject *parent) :
    QTcpSocket(parent)
{
    // //数据到达
    // connect(this, SIGNAL(readyRead()), this, SLOT(readyRead()));

    // //网络故障
    // connect(this, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error(QAbstractSocket::SocketError)));
}

PeerConnection::~PeerConnection()
{

}

// //网络故障
// void PeerConnection::error(QAbstractSocket::SocketError error)
// {
//     // //网络断开
//     // this->close();

//     // emit this->socketError(error);
// }

// //数据到达
// void PeerConnection::readyRead()
// {

// }
