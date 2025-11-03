#ifndef LITETHREAD_H
#define LITETHREAD_H

#include <QThread>
typedef std::function<void()> LPThreadWorkProc;
class QLiteThread :public QThread {
    Q_OBJECT

private:
    LPThreadWorkProc m_pfThreadWorkProc = 0;

public:
    explicit QLiteThread(QObject* parent = Q_NULLPTR, LPThreadWorkProc pfThreadWorkProc = Q_NULLPTR)
        : QThread(parent)
        , m_pfThreadWorkProc(pfThreadWorkProc)
    {
        //qRegisterMetaType<QVariant>("QVariant");
        connect(this, &QThread::finished, this, &QThread::deleteLater);
    }

    //析构函数
    ~QLiteThread()
    {
    }

    void setWorkThreadProc(LPThreadWorkProc pfThreadRun) {
        this->m_pfThreadWorkProc = pfThreadRun;
    }

protected:
    void run() override {
        m_pfThreadWorkProc();
    }

signals:
    //这里信号函数的参数个数可以根据自己需要随意增加
//    void invokeSignal();
//    void invokeSignal(QVariant);

public slots:

};

#endif // LITETHREAD_H
