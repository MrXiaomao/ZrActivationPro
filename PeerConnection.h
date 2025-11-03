#ifndef PEERCONNECTION_H
#define PEERCONNECTION_H

#include <QTcpSocket>

class PeerConnection : public QTcpSocket
{
    Q_OBJECT
public:
    explicit PeerConnection(QObject *parent = 0);
    ~PeerConnection();

// public slots:
//     void readyRead();
//     void error(QAbstractSocket::SocketError);

private:

};

#endif // PEERCONNECTION_H
