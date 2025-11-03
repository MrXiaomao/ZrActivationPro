#include "TcpConnection.h"
#include <QDataStream>

TcpConnection::TcpConnection(QObject *parent) :
    QTcpSocket(parent)
  , isConnected(false)
  , pksize(0)
{
    //连接成功
    connect(this, &QTcpSocket::connected, this, &TcpConnection::handleSocketConnected);
    //数据到达
    connect(this, &QIODevice::readyRead, this, &TcpConnection::handleSocketReceived);
    //网络故障
    typedef void (QAbstractSocket::*QAbstractSocketErrorSignal)(QAbstractSocket::SocketError);
    connect(this, static_cast<QAbstractSocketErrorSignal>(&QTcpSocket::error), this, &TcpConnection::handleSocketError);

    this->setReadBufferSize(4096);
    //启动重连线程
    connect(&this->reConnectTimer, SIGNAL(timeout()), this, SLOT(handleTimer()));  
}

void TcpConnection::disconnectFromHost()
{
    this->isConnected = false;
    this->enableReconectTimer(false);
    QTcpSocket::disconnectFromHost();
}

//启用重连模块
void TcpConnection::enableReconectTimer(bool enable)
{
    if (enable){
        this->reConnectTimer.start();
    }
    else{
        this->reConnectTimer.stop();
    }

    enableReconnectTimer = enable;
}
// 网络模块启动部分，以及连接至注册服务器
bool TcpConnection::initNetwork(QString srvIP, uint srvPort)
{
    // 连接至中心注册服务器
    if (this->isConnected && srvIP == this->serverIp && srvPort == this->serverPort)
        return true;

    this->serverIp = srvIP;
    this->serverPort = srvPort;
    this->abort();
    this->connectToHost(serverIp, serverPort);
    this->waitForConnected(1000);
    if (this->ConnectedState == QAbstractSocket::SocketState::UnconnectedState)
        this->isConnected = true;

    return this->isConnected;
}

void TcpConnection::handleSocketConnected()
{
    this->isConnected = true;
    this->reConnectTimer.stop();
}

/*
    --------------------------------------------------------
    datalen(4)+databuffer(sizevalue)
    --------------------------------------------------------
*/
void TcpConnection::handleSocketReceived()
{
    QDataStream ds(&this->pool, QIODevice::ReadWrite | QIODevice::Append);
    //读取新的数据
    QByteArray tmp = this->readAll();
    //将新数据追加到末尾
    this->pool.append(tmp);

    while (1){
        if (pksize==0){
            if ( this->pool.size() >= 4 ){
                //解析包大小
                ds.device()->seek(0);
                bool ok = false;
                pksize = this->pool.left(4).toHex().toInt(&ok, 16);
                this->pool.remove(0, 4);
            }
            else{
                break;
            }
        }

        if ( this->pool.size() >= pksize ){
            this->buffer = this->pool.left(pksize);
            this->pool.remove(0, pksize);
            pksize = 0;
            emit socketReceived(this, this->buffer);
            this->buffer.clear();
        }
        else{
            break;
        }
    }
}

void TcpConnection::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);

    //网络断开
    this->close();
    this->isConnected = false;
    this->enableReconectTimer(enableReconnectTimer);
}

void TcpConnection::handleTimer()
{
    if (!this->isConnected)
    {
        initNetwork(this->serverIp, this->serverPort);
    }
}

qint64 TcpConnection::send(const QByteArray &data)
{
    QByteArray tmp;
    QDataStream ds(&tmp, QIODevice::ReadWrite | QIODevice::Append);
    ds << data;

    qint64 len = this->write(tmp, tmp.size());
    this->waitForBytesWritten();
    //不用缓冲
    this->flush();
    return len;
}
