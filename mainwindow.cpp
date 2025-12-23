#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include "globalsettings.h"
#include "switchbutton.h"
#include <QTimer>

CentralWidget::CentralWidget(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::CentralWidget)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<MainWindow *>(parent))
{
    ui->setupUi(this);
    setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION);

    commHelper = CommHelper::instance();
    
    // 初始化测量倒计时定时器
    mMeasureCountdownTimer = new QTimer(this);
    mMeasureCountdownTimer->setSingleShot(false);
    mMeasureCountdownTimer->setInterval(1000); // 每秒触发一次
    connect(mMeasureCountdownTimer, &QTimer::timeout, this, &CentralWidget::onMeasureCountdownTimeout);
    
    initUi();
    initNet();

    restoreSettings();
    applyColorTheme();

    connect(this, SIGNAL(sigWriteLog(const QString&,QtMsgType)), this, SLOT(slotWriteLog(const QString&,QtMsgType)));

    ui->action_startServer->setEnabled(true);
    ui->action_stopServer->setEnabled(false);
    ui->action_connect->setEnabled(true);
    ui->action_powerOn->setEnabled(true);
    ui->action_powerOff->setEnabled(true);
    ui->action_disconnect->setEnabled(false);
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
    ui->action_autoMeasure->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
    ui->pushButton_stopMeasure->setEnabled(false);

    // 继电器
    connect(commHelper, &CommHelper::switcherConnected, this, [=](QString ip){
        // QLabel* label_Connected = this->findChild<QLabel*>("label_Connected");
        // label_Connected->setStyleSheet("color:#00ff00;");
        // label_Connected->setText(tr("交换机[%1]：已连通").arg(ip));
        ui->action_powerOn->setEnabled(true);
        ui->action_powerOff->setEnabled(true);
        qInfo().noquote() << tr("交换机[%1]：已连通").arg(ip);
    });
    connect(commHelper, &CommHelper::switcherDisconnected, this, [=](QString ip){
        // QLabel* label_Connected = this->findChild<QLabel*>("label_Connected");
        // label_Connected->setStyleSheet("color:#ff0000;");
        // label_Connected->setText(tr("交换机[%1]：断网").arg(ip));

        ui->action_powerOn->setEnabled(false);
        ui->action_powerOff->setEnabled(false);
        qCritical().noquote() << tr("交换机[%1]：断网").arg(ip);
    });

    // 探测器
    connect(commHelper, &CommHelper::detectorOnline, this, [=](quint8 index){
        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));
        //记录联网的探测器ID
        mOnlineDetectors.append(index);
        qInfo().nospace().nospace() << "Detector#" << index << ": online";
        //如果该探测器在温度超时报警列表中，则删除
        if (mTemperatureTimeoutDetectors.contains(index)){
            mTemperatureTimeoutDetectors.removeOne(index);
            //如果该探测器正在测量，则重新开始测量
            if (mDetectorMeasuring[index]){
                commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum, index);
                //打印日志
                qInfo().noquote() << "探测器" << index << "自动重新开始测量";
            }
        }
    });

    connect(commHelper, &CommHelper::detectorOffline, this, [=](quint8 index){
        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
        cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        // 探测器离线时，清除测量状态记录
        // mDetectorMeasuring[index] = false;
        // mDetectorMeasuring.remove(index);
        //清除联网的探测器ID
        mOnlineDetectors.removeOne(index);

        qInfo().nospace().nospace() << "Detector#" << index << ": offline";
    });

    //测量开始
    connect(commHelper, &CommHelper::measureStart, this, [=](quint8 index){
        ui->action_autoMeasure->setEnabled(false);

        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));

        // 重置该探测器的能谱数据
        resetDetectorSpectrum(index);

        // 记录探测器正在测量
        mDetectorMeasuring[index] = true;

        qInfo().nospace().nospace() << "Detector#" << index << ": the measurement is start, get ready to receive data";//开始实验，准备接收数据
    });

    //测量结束
    connect(commHelper, &CommHelper::measureStop, this, [=](quint8 index){
        ui->action_autoMeasure->setEnabled(true);

        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        // 记录探测器停止测量
        // mDetectorMeasuring[index] = false;

        qInfo().nospace().nospace() << "Detector#" << index << ": the measurement has been stopped";
    });

    //POE电源状态
    connect(commHelper, &CommHelper::reportPoePowerStatus, this, [=](quint8 index, bool on){
        int row = index - 1;
        SwitchButton* cell =  qobject_cast<SwitchButton*>(ui->tableWidget_detector->cellWidget(row, 0));
        cell->setChecked(on);
    });

    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        if(message.isEmpty()) {
            ui->statusbar->showMessage(tr("准备就绪"));
        } else {
            ui->statusbar->showMessage(message);
        }
    });
    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}

CentralWidget::~CentralWidget()
{
    GlobalSettings settings;
    // 保存触发模式
    settings.setValue("Trigger/Mode", ui->com_triggerModel->currentIndex());

    QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");// QSplitter(Qt::Horizontal,this);
    if (splitterH1){
        settings.setValue("Global/splitterH1/State", splitterH1->saveState());
        settings.setValue("Global/splitterH1/Geometry", splitterH1->saveGeometry());
    }

    QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
    if (splitterV2){
        settings.setValue("Global/splitterV2/State", splitterV2->saveState());
        settings.setValue("Global/splitterV2/Geometry", splitterV2->saveGeometry());
    }

    settings.setValue("Global/MainWindows/State", this->saveState());

    delete ui;
}

void CentralWidget::initUi()
{
    ui->leftStackedWidget->hide();

    // 隐藏页面【历史数据、数据分析、数据管理】
    ui->centralHboxTabWidget->setTabVisible(1, false);
    ui->centralHboxTabWidget->setTabVisible(2, false);
    ui->centralHboxTabWidget->setTabVisible(3, false);

    // 只显示一个谱仪
    if (0){
        ui->spectroMeterWidget2->setVisible(false);
        ui->spectroMeterWidget3->setVisible(false);
        ui->spectroMeterWidget4->setVisible(false);
        ui->spectroMeterWidget5->setVisible(false);
        ui->spectroMeterWidget6->setVisible(false);
    }

    // 触发类型
    {
        GlobalSettings settings;
        // 恢复触发模式
        int triggerMode = settings.value("Trigger/Mode", 0).toInt();
        ui->com_triggerModel->setCurrentIndex(triggerMode); // 或者从配置文件中读取
        onTriggerModelChanged(triggerMode);
    }

    connect(ui->com_triggerModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CentralWidget::onTriggerModelChanged);

    mClientPeersWindow = new ClientPeersWindow();
    // mClientPeersWindow->setAttribute(Qt::WA_TranslucentBackground);
    mClientPeersWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mClientPeersWindow->hide();
    connect(commHelper, &CommHelper::connectPeerConnection, mClientPeersWindow, &ClientPeersWindow::connectPeerConnection);
    connect(commHelper, &CommHelper::disconnectPeerConnection, mClientPeersWindow, &ClientPeersWindow::disconnectPeerConnection);

    mDetSettingWindow = new DetSettingWindow();
    mDetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mDetSettingWindow->hide();
    connect(commHelper, &CommHelper::connectPeerConnection, mDetSettingWindow, &DetSettingWindow::connectPeerConnection);
    connect(commHelper, &CommHelper::disconnectPeerConnection, mDetSettingWindow, &DetSettingWindow::disconnectPeerConnection);
    connect(mDetSettingWindow, &DetSettingWindow::settingfinished, commHelper, &CommHelper::settingfinished);

    QActionGroup *actionGrp = new QActionGroup(this);
    actionGrp->addAction(ui->action_waveformMode);
    actionGrp->addAction(ui->action_spectrumMode);

    QToolButton* pageupButton = new QToolButton(this);
    pageupButton->setText(tr("上一页"));
    pageupButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowUp));
    QToolButton* pagedownButton = new QToolButton(this);
    pagedownButton->setText(tr("下一页"));
    pagedownButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowDown));
    QWidget* tabWidgetButtonGroup = new QWidget(this);
    tabWidgetButtonGroup->setObjectName(QLatin1String("tabWidgetButtonGroup"));
    tabWidgetButtonGroup->setLayout(new QHBoxLayout(this));
    tabWidgetButtonGroup->layout()->setMargin(0);
    tabWidgetButtonGroup->layout()->setSpacing(0);
    tabWidgetButtonGroup->layout()->addWidget(pageupButton);
    tabWidgetButtonGroup->layout()->addWidget(pagedownButton);
    ui->centralHboxTabWidget->setCornerWidget(tabWidgetButtonGroup);

    connect(pageupButton, &QToolButton::clicked, this, [=](){
        if (1 == this->mCurrentPageIndex)
            return;

        this->mCurrentPageIndex--;
        for (int i=1; i<=6; ++i){
            QLabel* label_spectroMeter = this->findChild<QLabel*>(QString("label_spectroMeter%1").arg(i));
            if (label_spectroMeter)
                label_spectroMeter->setText(tr("谱仪#%1").arg((mCurrentPageIndex-1)*6 + i));
        }

        //清空图像数据
        {
            QMutexLocker locket(&mMutexSwitchPage);
            for (int i=1; i<=6; ++i){
                QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
                QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
                spectroMeter_top->graph(0)->data().clear();
                spectroMeter_bottom->graph(0)->data().clear();
            }
        }

        //展示当前页面的六个能谱图
        showSpectrumDisplay(mCurrentPageIndex);
        //展示当前页面的六个计数率图
        showCountRateDisplay(mCurrentPageIndex);
    });

    connect(pagedownButton, &QToolButton::clicked, this, [=](){
        if (4 == this->mCurrentPageIndex)
            return;

        this->mCurrentPageIndex++;
        for (int i=1; i<=6; ++i){
            QLabel* label_spectroMeter = this->findChild<QLabel*>(QString("label_spectroMeter%1").arg(i));
            if (label_spectroMeter)
                label_spectroMeter->setText(tr("谱仪#%1").arg((mCurrentPageIndex-1)*6 + i));
        }


        {
            QMutexLocker locket(&mMutexSwitchPage);
            for (int i=1; i<=6; ++i){
                QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
                QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
                spectroMeter_top->graph(0)->data().clear();
                spectroMeter_bottom->graph(0)->data().clear();
            }
        }

        //展示当前页面的六个能谱图
        showSpectrumDisplay(mCurrentPageIndex);
        //展示当前页面的六个计数率图
        showCountRateDisplay(mCurrentPageIndex);
    });

    connect(ui->centralHboxTabWidget,&QTabWidget::tabCloseRequested,this,[=](int index){
        if (index != 0){
            ui->centralHboxTabWidget->setTabVisible(index, false);
            QList<QAction*> actions;
            actions << ui->action_histroyData << ui->action_analyzeData << ui->action_managerData;
            actions[index-1]->setChecked(false);
        }
    });

    QActionGroup *actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_histroyData);
    actGroup->addAction(ui->action_analyzeData);
    actGroup->addAction(ui->action_managerData);
    connect(actGroup, &QActionGroup::triggered, this, [=](QAction *action){
        if (action == ui->action_histroyData)
            ui->centralHboxTabWidget->setTabVisible(1, true);
        else if (action == ui->action_analyzeData)
            ui->centralHboxTabWidget->setTabVisible(2, true);
        else if (action == ui->action_managerData)
            ui->centralHboxTabWidget->setTabVisible(3, true);
    });

    /*设置关闭按钮图标尺寸，QSS设置无效，所以只能在这里修改*/
    for (int i=0; i< ui->centralHboxTabWidget->count(); ++i)
    {
        ui->centralHboxTabWidget->tabBar()->tabButton(i, QTabBar::RightSide)->setFixedSize(QSize(12,12));
    }

    QActionGroup *themeActionGroup = new QActionGroup(this);
    ui->action_lightTheme->setActionGroup(themeActionGroup);
    ui->action_darkTheme->setActionGroup(themeActionGroup);
    ui->action_lightTheme->setChecked(!mIsDarkTheme);
    ui->action_darkTheme->setChecked(mIsDarkTheme);

    // 任务栏信息
    QLabel *label_Idle = new QLabel(ui->statusbar);
    label_Idle->setObjectName("label_Idle");
    label_Idle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Idle->setFixedWidth(300);
    label_Idle->setText(tr("准备就绪"));
    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        label_Idle->setText(message);
    });

    QLabel *label_LocalServer = new QLabel(ui->statusbar);
    label_LocalServer->setObjectName("label_LocalServer");
    label_LocalServer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_LocalServer->setFixedWidth(300);
    label_LocalServer->setText(tr("本地网络服务：未开启"));
    label_LocalServer->installEventFilter(this);

    QLabel *label_Connected = new QLabel(ui->statusbar);
    label_Connected->setObjectName("label_Connected");
    label_Connected->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Connected->setFixedWidth(300);
    //label_Connected->setText(tr("交换机网络状态：未开启"));
    label_Connected->installEventFilter(this);

    /*设置任务栏信息*/
    QLabel *label_systemtime = new QLabel(ui->statusbar);
    label_systemtime->setObjectName("label_systemtime");
    label_systemtime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->statusbar->setContentsMargins(5, 0, 5, 0);
    ui->statusbar->addWidget(label_Idle);
    ui->statusbar->addWidget(label_LocalServer);
    ui->statusbar->addWidget(label_Connected);
    ui->statusbar->addWidget(new QLabel(ui->statusbar), 1);
    ui->statusbar->addWidget(nullptr, 1);
    ui->statusbar->addPermanentWidget(label_systemtime);

    QTimer* systemClockTimer = new QTimer(this);
    systemClockTimer->setObjectName("systemClockTimer");
    connect(systemClockTimer, &QTimer::timeout, this, [=](){
        // 获取当前时间
        QDateTime currentDateTime = QDateTime::currentDateTime();

        // 获取星期几的数字（1代表星期日，7代表星期日）
        int dayOfWeekNumber = currentDateTime.date().dayOfWeek();

        // 星期几的中文名称列表
        QStringList dayNames = {
            tr("星期日"), QObject::tr("星期一"), QObject::tr("星期二"), QObject::tr("星期三"), QObject::tr("星期四"), QObject::tr("星期五"), QObject::tr("星期六"), QObject::tr("星期日")
        };

        // 根据数字获取中文名称
        QString dayOfWeekString = dayNames.at(dayOfWeekNumber);
        this->findChild<QLabel*>("label_systemtime")->setText(QString(QObject::tr("系统时间：")) + currentDateTime.toString("yyyy/MM/dd hh:mm:ss ") + dayOfWeekString);
    });
    systemClockTimer->start(900);

    QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
    splitterH1->setObjectName("splitterH1");
    splitterH1->setHandleWidth(5);
    splitterH1->addWidget(ui->leftStackedWidget);
    splitterH1->addWidget(ui->centralHboxTabWidget);
    splitterH1->addWidget(ui->rightVboxWidget);
    splitterH1->setSizes(QList<int>() << 100000 << 600000 << 100000);
    splitterH1->setCollapsible(0,false);
    splitterH1->setCollapsible(1,false);
    splitterH1->setCollapsible(2,false);
    ui->centralwidget->layout()->addWidget(splitterH1);
    ui->centralwidget->layout()->addWidget(ui->rightSidewidget);

    // 左侧栏
    QPushButton* detectorStatusButton = nullptr;
    {
        detectorStatusButton = new QPushButton();
        detectorStatusButton->setText(tr("设备信息"));
        detectorStatusButton->setFixedSize(250,29);
        detectorStatusButton->setCheckable(true);

        QHBoxLayout* sideHboxLayout = new QHBoxLayout();
        sideHboxLayout->setObjectName("sideHboxLayout");
        sideHboxLayout->setContentsMargins(0,0,0,0);
        sideHboxLayout->setSpacing(2);

        QWidget* sideProxyWidget = new QWidget();
        sideProxyWidget->setObjectName("sideProxyWidget");
        sideProxyWidget->setLayout(sideHboxLayout);
        sideHboxLayout->addWidget(detectorStatusButton);

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(sideProxyWidget);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->graphicsView->setScene(scene);
        ui->graphicsView->setFrameStyle(0);
        ui->graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setFixedSize(30, 250);
        ui->leftSidewidget->setFixedWidth(30);
    }

    // 右侧栏
    QPushButton* labPrametersButton = nullptr;
    {
        labPrametersButton = new QPushButton();
        labPrametersButton->setText(tr("实验参数"));
        labPrametersButton->setFixedSize(250,29);
        labPrametersButton->setCheckable(true);

        connect(labPrametersButton,&QPushButton::clicked,this,[=](){
            if(ui->rightVboxWidget->isHidden()) {
                ui->rightVboxWidget->show();

                GlobalSettings settings;
                settings.setValue("Global/ShowRightSide", "true");
            } else {
                ui->rightVboxWidget->hide();

                GlobalSettings settings;
                settings.setValue("Global/ShowRightSide", "false");
            }
        });

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(labPrametersButton);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->rightGraphicsView->setScene(scene);
        ui->rightGraphicsView->setFrameStyle(0);
        ui->rightGraphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setFixedSize(30, 250);
        ui->rightSidewidget->setFixedWidth(30);
    }

    ui->tableWidget_detector->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_detector->horizontalHeader()->setFixedHeight(25);
    ui->tableWidget_detector->setRowHeight(0, 30);
    for (int i=1; i<=3; ++i)
        ui->tableWidget_detector->setRowHeight(i, 30);
    ui->tableWidget_detector->setFixedHeight(750);

    for (int row=0; row<ui->tableWidget_detector->rowCount(); ++row){
        for (int column=0; column<=3; ++column){
            if (column == 0){
                SwitchButton* cell = new SwitchButton();
                cell->setAutoChecked(false);
                ui->tableWidget_detector->setCellWidget(row, column, cell);
                connect(cell, &SwitchButton::clicked, this, [=](){
                    if (!cell->getChecked()){
                        if (commHelper->openSwitcherPOEPower(row+1))
                        {
                            commHelper->manualOpenSwitcherPOEPower(row+1);
                            cell->setChecked(true);
                            //打印日志
                            qInfo().noquote() << "手动打开探测器" << row+1 << "的POE供电";
                        }
                    }
                    else{
                        if (commHelper->closeSwitcherPOEPower(row+1))
                        {
                            commHelper->manualCloseSwitcherPOEPower(row+1);
                            cell->setChecked(false);
                            //打印日志
                            qInfo().noquote() << "手动关闭探测器" << row+1 << "的POE供电";
                        }
                    }
                });
            }
            else {
                QLabel* cell = new QLabel();
                cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));
                cell->setAlignment(Qt::AlignCenter);
                ui->tableWidget_detector->setCellWidget(row, column, cell);
            }
        }
    }

    QAction *action = ui->lineEdit_filePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
    button->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button, &QToolButton::pressed, this, [=](){
        QString cacheDir = QFileDialog::getExistingDirectory(this);
        if (!cacheDir.isEmpty()){
            GlobalSettings settings(CONFIG_FILENAME);
            settings.setValue("Global/CacheDir", cacheDir);
            ui->lineEdit_filePath->setText(cacheDir);
        }
    });

    // 数据保存路径
    {
        GlobalSettings settings(CONFIG_FILENAME);
        QString cacheDir = settings.value("Global/CacheDir").toString();
        if (cacheDir.isEmpty())
            cacheDir = QApplication::applicationDirPath() + "/cache/";
        ui->lineEdit_filePath->setText(cacheDir);

        // 发次
        ui->spinBox_shotNum->setValue(settings.value("Global/ShotNum", 100).toUInt());
        ui->checkBox_autoIncrease->setChecked(settings.value("Global/ShotNumIsAutoIncrease", false).toBool());
    }

    QAction *action3 = ui->lineEdit_SaveAsPath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button3 = qobject_cast<QToolButton*>(action3->associatedWidgets().last());
    button3->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button3, &QToolButton::pressed, this, [=](){
        QString saveAsDir = QFileDialog::getExistingDirectory(this);
        if (!saveAsDir.isEmpty()){

            GlobalSettings settings(CONFIG_FILENAME);
            settings.setValue("Global/SaveAsPath", saveAsDir);

            ui->lineEdit_SaveAsPath->setText(saveAsDir);
        }
    });

    // 分析结果
    {
        GlobalSettings settings(CONFIG_FILENAME);
        QString saveAsDir = settings.value("Global/SaveAsPath").toString();
        if (saveAsDir.isEmpty())
            saveAsDir = QApplication::applicationDirPath() + "/cache/";
        ui->lineEdit_SaveAsPath->setText(saveAsDir);
        ui->lineEdit_SaveAsFileName->setText(settings.value("Global/SaveAsFileName", "test1").toString());
    }

    connect(detectorStatusButton,&QPushButton::clicked,this,[=](){
        if(ui->leftStackedWidget->isHidden()) {
            ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
            ui->leftStackedWidget->show();

            detectorStatusButton->setChecked(true);

            GlobalSettings settings;
            settings.setValue("Global/DefaultPage", "detectorStatus");
        } else {
            if(ui->leftStackedWidget->currentWidget() == ui->detectorStatusWidget) {
                ui->leftStackedWidget->hide();
                detectorStatusButton->setChecked(false);

                GlobalSettings settings;
                settings.setValue("Global/DefaultPage", "");
            } else {
                ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
                detectorStatusButton->setChecked(true);

                GlobalSettings settings;
                settings.setValue("Global/DefaultPage", "detectorStatus");
            }
        }
    });

    connect(ui->toolButton_closeDetectorStatusWidget,&QPushButton::clicked,this,[=](){
        ui->leftStackedWidget->hide();
        detectorStatusButton->setChecked(false);

        GlobalSettings settings;
        settings.setValue("Global/DefaultPage", "");
    });

    //恢复页面布局
    {
        GlobalSettings settings;
        QString defaultPage = settings.value("Global/DefaultPage").toString();
        if (defaultPage == "detectorStatus")
            detectorStatusButton->clicked();

        if (settings.contains("Global/MainWindows-State")){
            this->restoreState(settings.value("Global/MainWindows-State").toByteArray());
        }

        if (settings.value("Global/ShowRightSide").toString() == "false")
            ui->rightVboxWidget->hide();

        if (settings.contains("Global/splitterH1/State")){
            QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");
            if (splitterH1)
            {
                splitterH1->restoreState(settings.value("Global/splitterH1/State").toByteArray());
                //splitterH1->restoreGeometry(settings.value("Global/splitterH1/Geometry").toByteArray());
            }
        }

        if (settings.contains("Global/splitter/State")){
            QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
            if (splitterV2)
            {
                splitterV2->restoreState(settings.value("Global/splitterV2/State").toByteArray());
                //splitterV2->restoreGeometry(settings.value("Global/splitterV2/Geometry").toByteArray());
            }
        }
    }

    for (int i=1; i<=6; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        initCustomPlot(i, spectroMeter_top, tr("时间/s"), tr("计数"), tr("计数曲线"), 1);
        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        initCustomPlot(i, spectroMeter_bottom, tr("道址"), tr("计数"), tr("累积能谱"), 1);
    }

    QCustomPlot *spectroMeter_left = this->findChild<QCustomPlot*>(QString("spectroMeter_left"));
    initCustomPlot(100, spectroMeter_left, tr("时间/ns"), tr("ADC采样值"), tr("实时波形"), 1);
    QCustomPlot *spectroMeter_right = this->findChild<QCustomPlot*>(QString("spectroMeter_right"));
    initCustomPlot(101, spectroMeter_right, tr("道址"), tr("计数"), tr("累积能谱"), 1);

    //更新温度状态
    connect(commHelper, &CommHelper::reportDetectorTemperature, this, [=](quint8 index, float temperature){
        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 3));
        cell->setText(QString::number(temperature, 'f', 1) + " ℃");
        
        //将温度数据保存到文件中，实时写入，文件名称为探测器编号-日期.txt，路径为应用程序目录下的temperature文件夹中
        QString temperatureFilePath = QApplication::applicationDirPath() + "/temperature/" + QString::number(index) + "-" + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".txt";
        QFile temperatureFile(temperatureFilePath);
        if (!temperatureFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qWarning() << "Failed to open temperature file:" << temperatureFilePath;
            return;
        }
        QTextStream temperatureStream(&temperatureFile);
        temperatureStream << QString::number(temperature, 'f', 1) << " " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << endl;
        temperatureFile.close();

        if (temperature > 70.0) {
            cell->setStyleSheet("color: red;");
        } else {
            cell->setStyleSheet("color: green;");
        }
    }, Qt::QueuedConnection);
    
    connect(commHelper, &CommHelper::reportTemperatureTimeout, this, [=](quint8 index){
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(index-1, 3));
        cell->setStyleSheet("color: gray;");//灰色字体
        mTemperatureTimeoutDetectors.append(index);
    }, Qt::QueuedConnection);

    connect(commHelper, &CommHelper::reportSpectrumCurveData, this, [=](quint8 index, QVector<quint32>& data){
        Q_UNUSED(index);
        // 将 QVector<quint32> 转换为 int 数组
        if (data.size() != 8192) {
            qWarning() << "Detector" << index << "spectrum data size mismatch:" << data.size() << "expected 8192";
            return;
        }

        // 转换为 C 风格数组
        int spectrumData[8192];
        for (int i = 0; i < 8192; ++i) {
            spectrumData[i] = data[i];
        }

        // 更新探测器数据
        updateDetectorData(index, spectrumData);
    });

    connect(commHelper, &CommHelper::reportWaveformCurveData, this, [=](quint8 index, QVector<quint16>& data){
        Q_UNUSED(index);
        index = index - (mCurrentPageIndex-1)*6;
        if (index < 1 || index > 6)
            return;

        QVector<double> x, y;
        for(int i=0; i<data.size(); ++i)
        {
            x << i*10;
            y << data.at(i);
        }

        QMutexLocker locket(&mMutexSwitchPage);
        QCustomPlot *customPlot = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(index));
        customPlot->graph(0)->setData(x, y);
        customPlot->xAxis->rescale(true);
        customPlot->yAxis->rescale(true);
        //customPlot->yAxis->setRange(0, 10000);
        customPlot->replot(QCustomPlot::rpQueuedReplot);
    });

    // QTimer *timer = new QTimer(this);
    // static QElapsedTimer elapsedTimer;
    // elapsedTimer.start();
    // connect(timer, &QTimer::timeout, this, [=](){
    //     if (elapsedTimer.elapsed() > 10000)
    //         return;

    //     static quint64 base = 0;
    //     quint16 waveformLength = 512;
    //     quint16 period = 128;  //周期
    //     quint16 amplitude = 10000; // 振幅
    //     quint16 grain = 1;    //粒度
    //     QVector<double> x2, y2;
    //     for(quint64 x=base; x < base + waveformLength; x += grain)
    //     {
    //         double angle = (float) x / period * 2 * 3.1415926;
    //         quint16 data = (double)amplitude * sin(angle) + amplitude;

    //         QByteArray waveformBytes;
    //         waveformBytes.push_back(quint8((data >> 8) & 0xFF));
    //         waveformBytes.push_back(quint8(data & 0xFF));

    //         x2 << x - base;
    //         //y2 << data;
    //         bool ok;
    //         y2 << waveformBytes.mid(0, 2).toHex().toUShort(&ok, 16);
    //     }

    //     base++;
    //     ui->spectroMeter1_top->graph(0)->setData(x2, y2);
    //     ui->spectroMeter1_top->xAxis->rescale(true);
    //     ui->spectroMeter1_top->yAxis->rescale(true);
    //     //ui->spectroMeter1_top->yAxis->setRange(0, 4100);
    //     ui->spectroMeter1_top->replot(QCustomPlot::rpQueuedReplot);

    //     qDebug() << "elapsedTimer:" << elapsedTimer.elapsed() << base;
    // });
    // timer->start(10);
}

QPixmap CentralWidget::roundPixmap(QSize sz, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(0, 0, sz.width(), sz.height());
    painter.fillPath(bigCirclePath, QBrush(clrOut));

    return result;
}

QPixmap CentralWidget::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(1, 1, sz.width()-2, sz.height()-2);
    painter.setPen(QPen(QBrush(clrOut), 2, Qt::SolidLine));
    painter.drawPath(bigCirclePath);

    QPainterPath smallCirclePath;
    smallCirclePath.addEllipse(4, 4, sz.width() - 8, sz.height() - 8);
    painter.fillPath(smallCirclePath, QBrush(clrIn));

    return result;
}

void CentralWidget::initNet()
{
    connect(commHelper, &CommHelper::connectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){

    });
}

void CentralWidget::initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, 
        QString str_title, int graphCount/* = 1*/)
{
    customPlot->installEventFilter(this);
    customPlot->setProperty("index", index);
    
    // 创建标题文本元素
    QCPTextElement *title = new QCPTextElement(customPlot, str_title, QFont("Microsoft YaHei", 12, QFont::Bold));
    // 关键：把默认外边距/内边距压到最小
    title->setMargins(QMargins(0, 0, 0, 0));

    // 添加到图表布局（位于最上方）
    customPlot->plotLayout()->insertRow(0);
    customPlot->plotLayout()->addElement(0, 0, title);

    // 设置背景网格线是否显示
    //customPlot->xAxis->grid()->setVisible(true);
    //customPlot->yAxis->grid()->setVisible(true);
    // 设置背景网格线条颜色
    //customPlot->xAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));  // 垂直网格线条属性
    //customPlot->yAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine)); // 水平网格线条属性
    //customPlot->xAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::DotLine));
    //customPlot->yAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));

    // 设置全局抗锯齿
    customPlot->setAntialiasedElements(QCP::aeAll);
    // 图例名称隐藏
    customPlot->legend->setVisible(false);
    // customPlot->legend->setFillOrder(QCPLayoutGrid::foRowsFirst);//设置图例在一列中显示
    // customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignRight);// 图例名称显示位置
    // customPlot->legend->setBrush(Qt::NoBrush);//设置背景颜色
    // customPlot->legend->setBorderPen(Qt::NoPen);//设置边框颜色

    // 设置边界
    //customPlot->setContentsMargins(0, 0, 0, 0);
    // 设置标签倾斜角度，避免显示不下
    customPlot->xAxis->setTickLabelRotation(-45);
    // 允许拖拽，缩放
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // 允许轴自适应大小
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(false);
    // 设置刻度范围
    customPlot->xAxis->setRange(0, 200);
    customPlot->yAxis->setRange(0, 10000);
    customPlot->yAxis->ticker()->setTickCount(5);
    customPlot->xAxis->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    customPlot->yAxis2->ticker()->setTickCount(5);
    customPlot->xAxis2->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    //设置轴标签名称
    customPlot->xAxis->setLabel(str_title + axisXLabel);
    customPlot->yAxis->setLabel(axisYLabel);

    // 添加散点图
    QColor colors[] = {Qt::green, Qt::blue, Qt::red, Qt::cyan};
    for (int i=0; i<graphCount; ++i){
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        //graph->setName("");
        graph->setAntialiased(false);
        graph->setPen(QPen(colors[i]));
        graph->selectionDecorator()->setPen(QPen(colors[i]));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stNone);
        //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 2));//显示散点图
        //graph->setSmooth(true);
    }

    // 添加可选项
    if (graphCount > 1){
        static int index = 0;
        for (int i=0; i<graphCount; ++i){
            QCheckBox* checkBox = new QCheckBox(customPlot);
            checkBox->setText(QLatin1String("")+QString::number(++index));
            checkBox->setObjectName(QLatin1String("CH ")+QString::number(index));            

            QColor colors[] = {Qt::green, Qt::blue, Qt::red, Qt::cyan};
            checkBox->setIcon(QIcon(roundPixmap(QSize(16, 16), colors[i])));
            checkBox->setProperty("index", i+1);
            checkBox->setChecked(true);
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
                int index = checkBox->property("index").toInt();
                QCPGraph *graph = customPlot->graph(QLatin1String("Graph ")+QString::number(index));
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    customPlot->replot();
                }
            });
        }
        connect(customPlot, &QCustomPlot::afterLayout, this, [=](){
            QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(sender());
            QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
            int i = 0;
            for (auto checkBox : checkBoxs){
                checkBox->move(customPlot->axisRect()->topRight().x() - 70, customPlot->axisRect()->topRight().y() + i++ * 20 + 10);
            }
        });
    }

    // if (graphCount == 1){
    //     QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    //     customPlot->yAxis->setTicker(logTicker);
    //     //customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数

    //     customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis2->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis2->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis2->setNumberPrecision(0);//小数点后面小数位数
    // }
    // else {
    //     QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
    //     customPlot->yAxis->setTicker(ticker);
    // }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));


    // 是否允许X轴自适应缩放
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
    //connect(customPlot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(slotRestorePlot(QWheelEvent*)));
    //connect(customPlot, SIGNAL(mouseMove(QMouseEvent*)), this,SLOT(slotShowTracer(QMouseEvent*)));
}

void CentralWidget::closeEvent(QCloseEvent *event) {
    if (mIsMeasuring){
        QMessageBox::information(this, tr("系统退出提示"), tr("测量中禁止退出软件系统！"),
                                             QMessageBox::Ok, QMessageBox::Ok);
        event->ignore();
    }
    else
    {
        // 断开网络
        commHelper->stopMeasure();
        commHelper->stopServer();
        event->accept();
    }
}

bool CentralWidget::checkStatusTipEvent(QEvent * event) {
    if(event->type() == QEvent::StatusTip) {
        QStatusTipEvent* statusTipEvent = static_cast<QStatusTipEvent *>(event);
        if (!statusTipEvent->tip().isEmpty()) {
            ui->statusbar->showMessage(statusTipEvent->tip(), 2000);
        }

        return true;
    }

    return false;
}

bool CentralWidget::eventFilter(QObject *watched, QEvent *event){
    if (watched != this){
        if (event->type() == QEvent::MouseButtonPress){
            QMouseEvent *e = reinterpret_cast<QMouseEvent*>(event);
            if (watched->inherits("QCustomPlot")){
                QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(watched);

                if (e->button() == Qt::RightButton) {// 右键恢复
                    QMenu contextMenu(customPlot);
                    contextMenu.addAction(tr("恢复视图"), this, [=]{
                        customPlot->xAxis->rescale(true);
                        customPlot->yAxis->rescale(true);
                        customPlot->replot(QCustomPlot::rpQueuedReplot);
                    });
                    contextMenu.addAction(tr("导出图像..."), this, [=]{
                        QString filePath = QFileDialog::getSaveFileName(this);
                        if (!filePath.isEmpty()){
                            if (!filePath.endsWith(".png"))
                                filePath += ".png";
                            if (!customPlot->savePng(filePath, 1920, 1080))
                                QMessageBox::information(this, tr("提示"), tr("导出失败！"));
                        }
                    });
                    contextMenu.exec(QCursor::pos());

                    //释放内存
                    QList<QAction*> list = contextMenu.actions();
                    foreach (QAction* action, list) delete action;
                }                                
            }
            else if (watched->inherits("QLabel")){
                QLabel* label = qobject_cast<QLabel*>(watched);
                if (label->objectName() == "label_LocalServer"){
                    mClientPeersWindow->show();
                }
            }
        }

        else if (event->type() == QEvent::MouseButtonDblClick){
            /*QMouseEvent *e = reinterpret_cast<QMouseEvent*>(event);
            if (watched->inherits("QCustomPlot")){
                QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(watched);
                if (e->button() == Qt::LeftButton) {
                    mIsOneLayout = !mIsOneLayout;
                    if (mIsOneLayout)
                        ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageDetailWidget);
                    else
                        ui->stackedWidget->setCurrentWidget(ui->spectroMeterPageInfoWidget);

                    // int index = customPlot->property("index").toInt();
                    // for (int i = 1; i <= 6; ++i){
                    //     QWidget* spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(i));
                    //     if (i == index || !mIsOneLayout)
                    //         spectroMeterWidget->show();
                    //     else
                    //         spectroMeterWidget->hide();
                    // }
                }
            }*/
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void CentralWidget::slotWriteLog(const QString &msg, QtMsgType msgType)
{
    // 创建一个 QTextCursor
    QTextCursor cursor = ui->textEdit_log->textCursor();
    // 将光标移动到文本末尾
    cursor.movePosition(QTextCursor::End);

    // 先插入时间
    QString color = "black";
    if (mIsDarkTheme)
        color = "white";
    cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz >> ")));
    // 再插入文本
    if (msgType == QtDebugMsg || msgType == QtInfoMsg)
        cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, msg));
    else if (msgType == QtCriticalMsg || msgType == QtFatalMsg)
        cursor.insertHtml(QString("<span style='color:red;'>%1</span>").arg(msg));
    else
        cursor.insertHtml(QString("<span style='color:green;'>%1</span>").arg(msg));

    // 最后插入换行符
    cursor.insertHtml("<br>");

    // 确保 QTextEdit 显示了光标的新位置
    ui->textEdit_log->setTextCursor(cursor);

    //限制行数
    QTextDocument *document = ui->textEdit_log->document(); // 获取文档对象，想象成打开了一个TXT文件
    int rowCount = document->blockCount(); // 获取输出区的行数
    int maxRowNumber = 2000;//设定最大行
    if(rowCount > maxRowNumber){//超过最大行则开始删除
        QTextCursor cursor = QTextCursor(document); // 创建光标对象
        cursor.movePosition(QTextCursor::Start); //移动到开头，就是TXT文件开头

        for (int var = 0; var < rowCount - maxRowNumber; ++var) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor); // 向下移动并选中当前行
        }
        cursor.removeSelectedText();//删除选择的文本
    }
}


void CentralWidget::on_action_cfgParam_triggered()
{
    mDetSettingWindow->show();
}


void CentralWidget::on_action_exit_triggered()
{
    //qApp->quit();
    mainWindow->close();
}

void CentralWidget::on_action_open_triggered()
{
    // 打开历史测量数据文件...
    GlobalSettings settings;
    QString lastPath = settings.value("Global/LastFilePath", QDir::homePath()).toString();
    QString filter = "二进制文件 (*.dat);;文本文件 (*.csv);;所有文件 (*.dat *.csv)";
    QString filePath = QFileDialog::getOpenFileName(this, tr("打开测量数据文件"), lastPath, filter);

    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return;

    settings.setValue("Global/LastFilePath", filePath);
    if (!commHelper->openHistoryWaveFile(filePath))
    {
        QMessageBox::information(this, tr("提示"), tr("文件格式错误，加载失败！"));
    }
}


void CentralWidget::on_action_startServer_triggered()
{
    // 连接网络
    if (commHelper->startServer()){
        ui->action_startServer->setEnabled(false);
        ui->action_stopServer->setEnabled(true);
        // ui->action_connect->setEnabled(true);
        // ui->action_powerOn->setEnabled(true);
        // ui->action_powerOff->setEnabled(true);
        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(true);
        ui->pushButton_startMeasure->setEnabled(true);
        ui->pushButton_stopMeasure->setEnabled(true);
        ui->action_autoMeasure->setEnabled(true);

        QLabel* label_LocalServer = this->findChild<QLabel*>("label_LocalServer");
        label_LocalServer->setStyleSheet("color:#00ff00;");
        label_LocalServer->setText(tr("本地网络服务：已开启"));
    }
}


void CentralWidget::on_action_stopServer_triggered()
{
    //先记录下所有联网的探测器
    foreach (quint8 index, mOnlineDetectors){   
        commHelper->manualCloseSwitcherPOEPower(index);
        //将响应在线图标设置为红色
        int row = index - 1;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
        cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
    }
    //清除联网的探测器ID
    mOnlineDetectors.clear();
    //清除温度超时的探测器ID
    mTemperatureTimeoutDetectors.clear();

    // 断开网络
    commHelper->stopMeasure();
    commHelper->stopServer();

    ui->action_startServer->setEnabled(true);
    ui->action_stopServer->setEnabled(false);
    // ui->action_connect->setEnabled(false);
    // ui->action_powerOn->setEnabled(false);
    // ui->action_powerOff->setEnabled(false);
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
    ui->pushButton_stopMeasure->setEnabled(false);
    ui->action_autoMeasure->setEnabled(false);

    QLabel* label_LocalServer = this->findChild<QLabel*>("label_LocalServer");
    label_LocalServer->setStyleSheet("color:#ff0000;");
    label_LocalServer->setText(tr("本地网络服务：断开"));
}

/**
 * @brief CentralWidget::onTriggerModelChanged 触发模式选择：软件触发/外触发
 * @param index 下拉框序号，触发类型
 */
void CentralWidget::onTriggerModelChanged(int index)
{
    switch(index) {
    case 0: // 软件触发
        ui->label_7->setVisible(false);
        ui->lineEdit_triggerflag->setVisible(false);
        ui->dateTime_shotTime->setEnabled(true);
        break;
    case 1: // 外触发
        ui->label_7->setVisible(true);
        ui->lineEdit_triggerflag->setVisible(true);
        ui->dateTime_shotTime->setEnabled(false);
        break;
    default:
        break;
    }

    // 实时保存触发模式
    GlobalSettings settings;
    settings.setValue("Trigger/Mode", index);
}

void CentralWidget::on_action_startMeasure_triggered()
{
    QVector<double> keys, values;
    for (int i=1; i<=6; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        for (int j=0; j<spectroMeter_top->graphCount(); ++j)
            spectroMeter_top->graph(j)->data()->clear();

        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        for (int j=0; j<spectroMeter_bottom->graphCount(); ++j)
            spectroMeter_bottom->graph(j)->data()->clear();

        spectroMeter_top->replot();
        spectroMeter_bottom->replot();
    }

    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    quint32 shotNum = ui->spinBox_shotNum->value();

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNum);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNum));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    {
        GlobalSettings settings(QString("%1/Settings.ini").arg(savePath));
        settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
        settings.setValue("Global/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    }

    {
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
        settings.setValue("Global/CacheDir", ui->lineEdit_filePath->text());
    }

    commHelper->setShotInformation(shotDir, shotNum);

    // 读取倒计时时间（秒）
    int countdownSeconds = ui->spinBox_3->value();
    if (countdownSeconds > 0) {
        mRemainingCountdown = countdownSeconds;
        mTotalCountdown = countdownSeconds;
        mMeasureCountdownTimer->start();
        // 初始化测量时长显示为 00:00:00
        ui->edit_measureTime->setText("00:00:00");
        qInfo().noquote() << QString("开始测量，倒计时：%1 秒").arg(countdownSeconds);
    }

    // 再发开始测量指令
    if (ui->action_waveformMode->isChecked())
        commHelper->startMeasure(CommandAdapter::WorkMode::wmWaveform);
    else if (ui->action_spectrumMode->isChecked())
        commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum);
}


void CentralWidget::on_action_stopMeasure_triggered()
{
    // 停止倒计时定时器
    if (mMeasureCountdownTimer && mMeasureCountdownTimer->isActive()) {
        mMeasureCountdownTimer->stop();
        // 显示最终测量时长
        int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
        ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
        qInfo() << "测量倒计时已停止";
    } else {
        // 如果没有倒计时，清空显示
        ui->edit_measureTime->setText("00:00:00");
    }

    if (ui->checkBox_autoIncrease->isChecked()){
        ui->spinBox_shotNum->setValue(ui->spinBox_shotNum->value() + 1);
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
    }

    // 停止波形测量
    commHelper->stopMeasure();
    //清空温度超时报警的探测器ID
    mTemperatureTimeoutDetectors.clear();
    //清空探测器正在测量记录
    mDetectorMeasuring.clear();
}

void CentralWidget::onMeasureCountdownTimeout()
{
    mRemainingCountdown--;
    
    // 计算已测量时长（总时长 - 剩余倒计时）
    int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
    // 更新测量时长显示
    ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
    
    if (mRemainingCountdown > 0) {
        // 倒计时进行中，可以在这里更新UI显示剩余时间
        // qInfo().noquote() << QString("测量倒计时剩余：%1 秒").arg(mRemainingCountdown);
    } else {
        // 倒计时结束
        mMeasureCountdownTimer->stop();
        // 显示最终测量时长
        ui->edit_measureTime->setText(formatTimeString(mTotalCountdown));
        qInfo() << "测量倒计时结束，正在停止测量并关闭所有通道电源...";
        
        // 停止所有测量
        commHelper->stopMeasure();
        
        //记录下关闭的电源通道号
        foreach (quint8 index, mOnlineDetectors){
            commHelper->manualCloseSwitcherPOEPower(index);
        }
        // 关闭所有通道电源
        commHelper->closePower();
        
        //清空温度超时报警的探测器ID
        mTemperatureTimeoutDetectors.clear();
        //清空探测器正在测量记录
        mDetectorMeasuring.clear();
        
        qInfo() << "所有测量已停止，所有通道电源已关闭";
    }
}


void CentralWidget::on_action_powerOn_triggered()
{
    //清除所有
    commHelper->manualOpenSwitcherPOEPower();

    // 打开电源
    commHelper->openPower();
}


void CentralWidget::on_action_powerOff_triggered()
{
    // 关闭电源
    commHelper->closePower();
}

void CentralWidget::on_pushButton_startMeasureDistance_clicked()
{

}

void CentralWidget::on_pushButton_stopMeasureDistance_clicked()
{

}

void CentralWidget::on_action_about_triggered()
{
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QMessageBox::about(this, tr("关于"),
                       QString("<p>") +
                           tr("版本") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(filename, APP_VERSION) +
                           tr("提交") +
                           QString("</p><span style='color:blue;'>%1: %2</span><p>").arg(GIT_BRANCH, GIT_HASH) +
                           tr("日期") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(GIT_DATE) +
                           tr("开发者") +
                           QString("</p><span style='color:blue;'>MaoXiaoqing</span><p>") +
                           "</p><p>四川大学物理学院 版权所有 (C) 2025</p>"
                       );
}

void CentralWidget::on_action_aboutQt_triggered()
{
    QMessageBox::aboutQt(this);
}


void CentralWidget::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void CentralWidget::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void CentralWidget::on_action_colorTheme_triggered()
{
    GlobalSettings settings;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settings.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settings.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

void CentralWidget::applyColorTheme()
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        QPalette palette = customPlot->palette();
        if (mIsDarkTheme)
        {
            if (this->mThemeColorEnable)
            {
                CustomColorDarkStyle darkStyle(mThemeColor);
                darkStyle.polish(palette);
            }
            else
            {
                DarkStyle darkStyle;
                darkStyle.polish(palette);
            }

            // 创建一个 QTextCursor
            QTextCursor cursor = ui->textEdit_log->textCursor();
            QTextDocument *document = cursor.document();
            QString html = document->toHtml();
            html = html.replace("color:#000000", "color:#ffffff");
            document->setHtml(html);
        }
        else
        {
            if (this->mThemeColorEnable)
            {
                CustomColorLightStyle lightStyle(mThemeColor);
                lightStyle.polish(palette);
            }
            else
            {
                LightStyle lightStyle;
                lightStyle.polish(palette);
            }

            QTextCursor cursor = ui->textEdit_log->textCursor();
            QTextDocument *document = cursor.document();
            QString html = document->toHtml();
            html = html.replace("color:#ffffff", "color:#000000");
            document->setHtml(html);
        }
        //日志窗体
        QString styleSheet = mIsDarkTheme ?
                                 QString("background-color:rgb(%1,%2,%3);color:white;")
                                    .arg(palette.color(QPalette::Dark).red())
                                    .arg(palette.color(QPalette::Dark).green())
                                    .arg(palette.color(QPalette::Dark).blue())
                                : QString("background-color:white;color:black;");
        //更新样式表
        QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
        for (auto checkBox : checkBoxs){
            checkBox->setStyleSheet(styleSheet);
        }

        // 更新工作日志标签颜色
        QLabel* workLogLabel = this->findChild<QLabel*>("label_workLog");
        if (workLogLabel) {
            QString style = mIsDarkTheme ? "color: white;" : "color: black;";
            workLogLabel->setStyleSheet(style);
        }

        // 窗体背景色
        customPlot->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 四边安装轴并显示
        customPlot->axisRect()->setupFullAxesBox();
        customPlot->axisRect()->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 坐标轴线颜色
        customPlot->xAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // 刻度线颜色
        customPlot->xAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // 子刻度线颜色
        customPlot->xAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // 坐标轴文本标签颜色
        customPlot->xAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setLabelColor(palette.color(QPalette::WindowText));
        // 坐标轴刻度文本标签颜色
        customPlot->xAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setTickLabelColor(palette.color(QPalette::WindowText));

        customPlot->replot();
    }
}

void CentralWidget::restoreSettings()
{
    GlobalSettings settings;
    if(mainWindow) {
        mainWindow->restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        mainWindow->restoreState(settings.value("MainWindow/State").toByteArray());
    } else {
        restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        restoreState(settings.value("MainWindow/State").toByteArray());
    }
    mThemeColor = settings.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    mThemeColorEnable = settings.value("Global/Startup/themeColorEnable",true).toBool();
    if(mThemeColorEnable) {
        QTimer::singleShot(0, this, [&](){
            qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
            QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
        });
    }
}

#include <QFile>
#include <QTextStream>
#include <QVector>
bool CentralWidget::openXRDFile(const QString &filename, QVector<QPair<double, double>>& data){
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开文件:" << filename;
        return false;
    }

    QTextStream in(&file);
    // 可选：设置编码
    in.setCodec("UTF-8");

    int lineNumber = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        lineNumber++;

        // 跳过空行和注释行
        if (line.trimmed().isEmpty() || line.startsWith('#')) {
            continue;
        }

        // 分割CSV行（支持逗号或分号分隔）
        QStringList parts = line.split(',', Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            parts = line.split(';', Qt::SkipEmptyParts);
        }

        if (parts.size() < 2) {
            qDebug() << "第" << lineNumber << "行数据列数不足，跳过";
            continue;
        }

        // 转换为double
        bool ok1, ok2;
        double value1 = parts[0].trimmed().toDouble(&ok1);
        double value2 = parts[1].trimmed().toDouble(&ok2);

        if (ok1 && ok2) {
            data.append(QPair<double, double>(value1, value2));
        } else {
            qDebug() << "第" << lineNumber << "行数据转换失败:" << line;
        }
    }

    file.close();
    return true;
}

void CentralWidget::on_pushButton_saveAs_clicked()
{
    QString strSavePath = QString("%1/%2").arg(ui->lineEdit_SaveAsPath->text(), ui->lineEdit_SaveAsFileName->text());
    if (commHelper->saveAs(strSavePath))
    {
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/SaveAsFileName", ui->lineEdit_SaveAsFileName->text());
        settings.setValue("Global/SaveAsPath", ui->lineEdit_SaveAsPath->text());
        QMessageBox::information(this, tr("提示"), tr("保存成功！"));
    }
    else
    {
        QMessageBox::information(this, tr("提示"), tr("保存失败！"));
    }
}

// 更新探测器数据
void CentralWidget::updateDetectorData(int detectorId, const int newSpectrum[]) {
    if (detectorId < 1 || detectorId > 24) {
        qWarning() << "Invalid detector ID:" << detectorId;
        return;
    }

    DetectorData &data = m_detectorData[detectorId];

    // 累积能谱
    double countRate = 0.0;
    for (int i = 0; i < 8192; ++i) {
        countRate += newSpectrum[i];
        data.spectrum[i] += newSpectrum[i];
    }

    // 更新计数率
    data.countRateHistory.append(countRate);

    // 更新界面显示
    updateSpectrumDisplay(detectorId, data.spectrum);

    // 更新计数率显示（如果有对应的图表）
    updateCountRateDisplay(detectorId, countRate);
}

// 重置能谱
void CentralWidget::resetDetectorSpectrum(int detectorId) {
    auto it = m_detectorData.find(detectorId);
    if (it == m_detectorData.end())
        return;

    // 清空能谱
    for (int i = 0; i < 8192; ++i)
        it->spectrum[i] = 0;

    // 清空计数率历史并释放内存
    QVector<double>().swap(it->countRateHistory);
    // 如果是 QList<double>，同样写法：QList<double>().swap(it->countRateHistory);

    qInfo() << "Detector" << detectorId << "spectrum and countRateHistory reset";
}

// 获取探测器数据
DetectorData CentralWidget::getDetectorData(int detectorId) const {
    return m_detectorData.value(detectorId, DetectorData{});
}

// 检查探测器是否在线
bool CentralWidget::isDetectorOnline(int detectorId) const {
    return m_detectorData.contains(detectorId);
}

// 获取在线探测器列表
QList<int> CentralWidget::getOnlineDetectors() const {
    return m_detectorData.keys();
}

// 更新能谱、计数率显示
void CentralWidget::updateSpectrumDisplay(int detectorId, const quint32 spectrum[]) {
    // 计算在页面中的索引
    int displayIndex = detectorId - (mCurrentPageIndex - 1) * 6;
    if (displayIndex < 1 || displayIndex > 6) {
        return;  // 不在当前显示页面
    }

    QMutexLocker locker(&mMutexSwitchPage);
    QCustomPlot *customPlot = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(displayIndex));
    if (!customPlot) return;

    // 转换数据格式
    QVector<double> x, y;
    for (int i = 0; i < 8192; ++i) {
        x << i;
        y << spectrum[i];
    }

    // 更新图表
    customPlot->graph(0)->setData(x, y);
    customPlot->xAxis->setRange(0, 8192);
    customPlot->yAxis->rescale(true);

    double y_min = customPlot->yAxis->range().lower;
    double y_max = customPlot->yAxis->range().upper;
    y_max = y_min + (y_max - y_min) * 1.1;
    customPlot->yAxis->setRange(y_min - 1, y_max);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

// 添加计数率显示更新函数
void CentralWidget::updateCountRateDisplay(int detectorId, double countRate) {
    // 计算在页面中的索引
    int displayIndex = detectorId - (mCurrentPageIndex - 1) * 6;
    if (displayIndex < 1 || displayIndex > 6) {
        return;  // 不在当前显示页面
    }
    DetectorData &data = m_detectorData[detectorId];
    QMutexLocker locker(&mMutexSwitchPage);
    QCustomPlot *customPlot = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(displayIndex));
    if (!customPlot) return;

    // 获取当前时间（使用相对时间或绝对时间）
    static QElapsedTimer timer;
    if (!timer.isValid()) {
        timer.start();
    }
    // double currentTime = timer.elapsed() / 1000.0; // 转换为秒

    // int currentTime = data.countRateHistory.size();
    // 添加新的数据点
    int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
    qDebug().noquote()<<"detID="<<detectorId<<", elapsedTime = "<<elapsedSeconds;
    customPlot->graph(0)->addData(elapsedSeconds, countRate);

    // 显示最近300秒
    const int WINDOW = 300;
    customPlot->xAxis->setRange(qMax(0.0, elapsedSeconds*1.0 - WINDOW), elapsedSeconds + 1);
    
    // y轴范围只由最近300秒的y决定
    auto range = calcRecentYRange(data.countRateHistory, WINDOW);
    double y_min = range.first;
    double y_max = range.second;

    // 稍微留点头部空间
    double pad = (y_max - y_min) * 0.1;
    customPlot->yAxis->setRange(y_min - pad, y_max + pad);

    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

// 展示当前页面的六个能谱图
void CentralWidget::showSpectrumDisplay(int currentPageIndex)
{
    // 备份当前页面索引，暂时切换到要展示的页面，方便复用 updateSpectrumDisplay
    int oldPageIndex = mCurrentPageIndex;
    mCurrentPageIndex = currentPageIndex;

    int startDetectorId = (currentPageIndex - 1) * 6 + 1;
    int endDetectorId   = startDetectorId + 5;

    for (int detectorId = startDetectorId; detectorId <= endDetectorId; ++detectorId) {
        int displayIndex = detectorId - (currentPageIndex - 1) * 6; // 1~6

        QMutexLocker locker(&mMutexSwitchPage);
        QCustomPlot *plot = this->findChild<QCustomPlot*>(
            QString("spectroMeter%1_bottom").arg(displayIndex));
        if (!plot) {
            continue;
        }

        // 如果这个探测器有数据，就画出来；否则清空这一个图
        auto it = m_detectorData.find(detectorId);
        if (it != m_detectorData.end()) {
            // 复用已有的更新逻辑
            locker.unlock(); // updateSpectrumDisplay 内部也会加锁
            updateSpectrumDisplay(detectorId, it->spectrum);
        } else {
            plot->graph(0)->data()->clear();
            plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }

    // 恢复原来的页面索引
    mCurrentPageIndex = oldPageIndex;
}

// 展示当前页面的六个计数率图
void CentralWidget::showCountRateDisplay(int currentPageIndex)
{
    int startDetectorId = (currentPageIndex - 1) * 6 + 1;
    int endDetectorId   = startDetectorId + 5;

    for (int detectorId = startDetectorId; detectorId <= endDetectorId; ++detectorId) {
        int displayIndex = detectorId - (currentPageIndex - 1) * 6; // 1~6

        QMutexLocker locker(&mMutexSwitchPage);
        QCustomPlot *plot = this->findChild<QCustomPlot*>(
            QString("spectroMeter%1_top").arg(displayIndex));
        if (!plot) {
            continue;
        }

        auto it = m_detectorData.find(detectorId);
        if (it == m_detectorData.end() || it->countRateHistory.isEmpty()) {
            // 没有数据，清空
            plot->graph(0)->data()->clear();
            plot->replot(QCustomPlot::rpQueuedReplot);
            continue;
        }

        // 用历史计数率重绘整条曲线
        QVector<double> x, y;
        x.reserve(it->countRateHistory.size());
        y.reserve(it->countRateHistory.size());

        // 这里用采样序号作为 X 轴（0,1,2,...），
        // 如果你有真实时间戳，也可以改成时间。
        for (int i = 0; i < it->countRateHistory.size(); ++i) {
            x << i;
            y << it->countRateHistory[i];
        }

        plot->graph(0)->setData(x, y);

        // X 轴范围：完整显示历史数据
        plot->xAxis->setRange(0, qMax(1, x.size()));

        // Y 轴自适应
        plot->yAxis->rescale(true);
        double y_min = plot->yAxis->range().lower;
        double y_max = plot->yAxis->range().upper;
        y_max = y_min + (y_max - y_min) * 1.1;
        plot->yAxis->setRange(y_min - 1, y_max);

        plot->replot(QCustomPlot::rpQueuedReplot);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////
MainWindow::MainWindow(bool isDarkTheme, QWidget *parent)
    : QGoodWindow(parent) {
    mCentralWidget = new CentralWidget(isDarkTheme, this);
    mCentralWidget->setWindowFlags(Qt::Widget);
    mGoodCentraWidget = new QGoodCentralWidget(this);

#ifdef Q_OS_MAC
    //macOS uses global menu bar
    if(QApplication::testAttribute(Qt::AA_DontUseNativeMenuBar)) {
#else
    if(true) {
#endif
        mMenuBar = mCentralWidget->menuBar();
        if (mMenuBar)
        {
            //Set font of menu bar
            QFont font = mMenuBar->font();
#ifdef Q_OS_WIN
            font.setFamily("Segoe UI");
#else
            font.setFamily(qApp->font().family());
#endif
            mMenuBar->setFont(font);

            QTimer::singleShot(0, this, [&]{
                const int title_bar_height = mGoodCentraWidget->titleBarHeight();
                mMenuBar->setStyleSheet(QString("QMenuBar {height: %0px;}").arg(title_bar_height));
            });

            connect(mGoodCentraWidget,&QGoodCentralWidget::windowActiveChanged,this, [&](bool active){
                mMenuBar->setEnabled(active);
            });

            mGoodCentraWidget->setLeftTitleBarWidget(mMenuBar);
        }
    }

    connect(qGoodStateHolder, &QGoodStateHolder::currentThemeChanged, this, [](){
        if (qGoodStateHolder->isCurrentThemeDark())
            QGoodWindow::setAppDarkTheme();
        else
            QGoodWindow::setAppLightTheme();
    });
    connect(this, &QGoodWindow::systemThemeChanged, this, [&]{
        qGoodStateHolder->setCurrentThemeDark(QGoodWindow::isSystemThemeDark());
    });
    qGoodStateHolder->setCurrentThemeDark(isDarkTheme);

    mGoodCentraWidget->setCentralWidget(mCentralWidget);
    setCentralWidget(mGoodCentraWidget);

    setWindowIcon(QIcon(":/logo.png"));
    setWindowTitle(mCentralWidget->windowTitle());

    mGoodCentraWidget->setTitleAlignment(Qt::AlignCenter);
}

MainWindow::~MainWindow() {
    delete mCentralWidget;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    mCentralWidget->closeEvent(event);
}

bool MainWindow::event(QEvent * event) {
    if(event->type() == QEvent::StatusTip) {
        mCentralWidget->checkStatusTipEvent(static_cast<QStatusTipEvent *>(event));
        return true;
    }

    return QGoodWindow::event(event);
}

#include "localsettingwindow.h"
void CentralWidget::on_action_localService_triggered()
{
    LocalSettingWindow *w = new LocalSettingWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog);
    w->setWindowModality(Qt::ApplicationModal);
    w->showNormal();
}

//单通道测量，单独测量某个通道
void CentralWidget::on_pushButton_startMeasure_clicked()
{
    QVector<double> keys, values;
    for (int i=1; i<=6; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        for (int j=0; j<spectroMeter_top->graphCount(); ++j)
            spectroMeter_top->graph(j)->data()->clear();

        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        for (int j=0; j<spectroMeter_bottom->graphCount(); ++j)
            spectroMeter_bottom->graph(j)->data()->clear();

        spectroMeter_top->replot();
        spectroMeter_bottom->replot();
    }

    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    quint32 shotNum = ui->spinBox_shotNum->value();

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNum);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNum));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    {
        GlobalSettings settings(QString("%1/Settings.ini").arg(savePath));
        settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
        settings.setValue("Global/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    }

    {
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("Global/ShotNum", ui->spinBox_shotNum->value());
        settings.setValue("Global/CacheDir", ui->lineEdit_filePath->text());
    }

    commHelper->setShotInformation(shotDir, shotNum);

    // 再发开始测量指令
    auto rows  = ui->tableWidget_detector->selectionModel()->selectedRows();
    if (rows .isEmpty()) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
    }
    // 为了稳定，排序一下（可选）
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b){ return a.row() < b.row(); });

    if (ui->action_waveformMode->isChecked())
    {
        qInfo()<<"自定义通道测量，波形测量模式";
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : rows) {
            int row = mi.row();
            quint8 index = row + 1;
            str += QString("%1, ").arg(index);
            commHelper->startMeasure(CommandAdapter::WorkMode::wmWaveform, index);
        }
        qInfo()<<str;
    }
    else if (ui->action_spectrumMode->isChecked())
    {
        qInfo()<<"自定义通道测量，能谱测量模式";
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : rows) {
            int row = mi.row();
            quint8 index = row + 1;
            str += QString("%1, ").arg(index);
            commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum, index);
        }
        qInfo()<<str;
    }

    //记录下所选的通道号，保存到成员变量中，停止测量的时候使用并清空
    m_selectedChannels.clear();
    for (const auto &mi : rows) {
        int row = mi.row();
        quint8 index = row + 1;
        m_selectedChannels.append(index);
    }
}


QString CentralWidget::formatTimeString(int totalSeconds)
{
    int days = totalSeconds / 86400;  // 86400秒 = 24小时
    int remainingSeconds = totalSeconds % 86400;
    int hours = remainingSeconds / 3600;
    int minutes = (remainingSeconds % 3600) / 60;
    int seconds = remainingSeconds % 60;
    
    if (days > 0) {
        // 超过24小时，显示天数
        return QString("%1day %2:%3:%4")
                .arg(days)
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
    } else {
        // 24小时以内，只显示时分秒
        return QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
    }
}

void CentralWidget::on_pushButton_stopMeasure_clicked()
{
    for (const auto &index : m_selectedChannels)
    {
        commHelper->stopMeasure(index);
    }

    //打印日志
    qInfo()<<"点击(自定义通道)停止测量";

    //清空所选通道号列表
    m_selectedChannels.clear();
}


void CentralWidget::on_action_connect_triggered()
{
    // 打开电源
    commHelper->connectSwitcher();
}

