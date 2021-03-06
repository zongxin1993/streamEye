#include "mainwindow.h"
#include "ui_mainwindow.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    //, ui(new Ui::MainWindow)
{
    ui = new Ui::MainWindow;
    ui->setupUi(this);
    load_flag = false;
    m_pLayout = new QGridLayout();
    analyzeMode = BITRATE_MODE_NULL;

    //click Menu group signal and slot
    connect(ui->menubar,SIGNAL(triggered(QAction*)),this,SLOT(On_ClickedMenuActionGroup(QAction *)));

}

MainWindow::~MainWindow()
{
    delete ui;
}

/*
FUNC: Clicked menu action group slot
Param: [0]: QAction*
Return: NULL
*/
void MainWindow::On_ClickedMenuActionGroup(QAction *action)
{

    // menu tools "Open"
    if(action->text().compare("Open")==0) {
        filename = "";
        filename = QFileDialog::getOpenFileName();
        // init status
        if (filename != "" && load_flag) {
            load_flag = false;
            delete m_chartView;
            m_tooltip = nullptr;
        }
    } else if (action->text().compare("Close")==0) {
        qApp->quit();
    } else if (action->text().compare("Frame of Bitrate")==0) {  // Frame of bitrate
        if (filename != "" && !load_flag) {
            variable_init();
            load_flag = ffmpegTool::probe_get_stream_info(&info, &PacketList, filename);
        } else {
            // clean old var
            delete m_chartView;
            m_tooltip = nullptr;
        }
        // creat chart of bitrate
        analyzeMode = FRAME_OF_BITRATE;
        creat_bitrate_chart();
    } else if (action->text().compare("GOP of Bitrate")==0) { // GOP of bitrate
        if (filename != "" && !load_flag) {
            variable_init();
            load_flag = ffmpegTool::probe_get_stream_info(&info, &PacketList, filename);
        } else {
            // clean old var
            delete m_chartView;
            m_tooltip = nullptr;
        }
        // creat chart of bitrate
        analyzeMode = GOP_OF_BITRATE;
        creat_bitrate_chart();
    } else if (action->text().compare("Second of Bitrate")==0) { // Second of bitrate
        if (filename != "" && !load_flag) {
            variable_init();
            load_flag = ffmpegTool::probe_get_stream_info(&info, &PacketList, filename);
        } else {
            // clean old var
            delete m_chartView;
            m_tooltip = nullptr;
        }
        // creat chart of bitrate
        analyzeMode = SECOND_OF_BITRATE;
        creat_bitrate_chart();
    } else {

    }
}

/*
FUNC: var init
Param: NULL
Return: NULL
*/
void MainWindow::variable_init()
{
    PacketList.clear();
    memset(&info,0,sizeof(info));
}

void MainWindow::creat_bitrate_chart(){

    // bitrate average
    float bitrate_average = 0.0;
    for (unsigned long var = 0; var < PacketList.size(); ++var) {
       bitrate_average += PacketList[var].bitrate;
    }
    bitrate_average = bitrate_average / PacketList.size();

    // mode and pace for bitrate statistic
    unsigned int face = 0;
    if (analyzeMode == GOP_OF_BITRATE) {
        face = info.gop_size;
    } else if (analyzeMode == FRAME_OF_BITRATE) {
        face = 1;
    } else if (analyzeMode == SECOND_OF_BITRATE) {
        face = (int)(info.fps * 10 + 5) / 10;
    } else {
        face = 0;
    }

    // set bitrate value to barset
    QBarSet *barSet = new QBarSet("Bitrate");
    unsigned int quotient = PacketList.size() / face;
    unsigned int remainde = PacketList.size() % face;
    float bitrate_tmp = 0;

    for (unsigned long var = 0; var < quotient * face;) {
        bitrate_tmp = 0;
        for (unsigned int i = 0;i < face; i++) {
            bitrate_tmp = bitrate_tmp + PacketList[var++].bitrate;
        }
        barSet->insert(var/face, bitrate_tmp / face);
    }

    if (0 != remainde) {
        bitrate_tmp = 0;
        for (unsigned long var = quotient*face; var < PacketList.size();var++) {
            bitrate_tmp = bitrate_tmp + PacketList[var].bitrate;
        }
        barSet->insert(quotient+1, bitrate_tmp/remainde);
    }

    //set bar value
    QBarSeries *barseries = new QBarSeries();
    barseries->append(barSet);

    // touch signal and slot
    connect(barseries, SIGNAL(hovered(bool, int, QBarSet*)), this, SLOT(sltTooltip(bool, int, QBarSet*)));

    // bitrate average
    QLineSeries *lineseries = new QLineSeries();
    lineseries->setName("Bitrate average");
    for (unsigned long var = 0; var < PacketList.size() / face; ++var) {
       lineseries->append(QPoint(var, bitrate_average));
    }

    // creat chart
    QChart *chart = new QChart();
    chart->addSeries(barseries);
    chart->addSeries(lineseries);
    chart->createDefaultAxes();
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    // creat chart view;
    m_chartView = new QChartView(chart);
    QSize chartSize = QSize((quotient + 1)*15,400);
    m_chartView->setMinimumSize(chartSize);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    // set chartview to scrollarea
    m_pLayout->addWidget(m_chartView);
    ui->workscroll_widget->setLayout(m_pLayout);

    // set stream info
   ui->streamFormatLabel->setText("Format:  " + QString(info.codec_name));
   ui->streamLevelLabel->setText("Level:  " + QString::number(info.level));
   ui->streamFPSLabel->setText("FPS:  " + QString::number(info.fps));
   ui->streamPixFmtLabel->setText("Pix_fmt:  " + QString(info.pix_fmt));
   ui->streamWidthLabel->setText("Width:  " + QString::number(info.width));
   ui->streamHightLabel->setText("Height:  " + QString::number(info.height));
   ui->streamCountLabel->setText("Frame_count:  " + QString::number(info.frame_count));
   ui->streamBitrateLabel->setText("Bitrate:  " + QString::number(bitrate_average));
   ui->streamMaxBitrateLabel->setText("Max_bitrate:  " + QString::number(info.max_bitrate));
   ui->streamMinBitrateLabel->setText("Min_bitrate:  " + QString::number(info.min_bitrate));
   ui->streamGOPLabel->setText("GOP:  " + QString::number(info.gop_size));
}

/*
FUNC: Clicked chart slot
Param: [0]: bool
       [1]: chart date index*
       [2]: QBarSet*
Return: NULL
*/
void MainWindow::sltTooltip(bool status, int index, QBarSet *barset)
{
    //Prompt for numeric text when mouse points to chart column
    QChart* pchart = m_chartView->chart();
    if(m_tooltip == nullptr) {
        m_tooltip = new QLabel(m_chartView);
        m_tooltip->setStyleSheet("background: rgba(95,166,250,185);color: rgb(248, 248, 255);"
                                 "border:0px groove gray;border-radius:10px;padding:2px 4px;"
                                 "border:2px groove gray;border-radius:10px;padding:2px 4px");
        m_tooltip->setVisible(false);
    }
    if (status) {
        double val = barset->at(index);
        QPointF point(index, barset->at(index));
        QPointF pointLabel = pchart->mapToPosition(point);
        QString sText = QString("%1Kbps").arg(val);

        m_tooltip->setText(sText);
        m_tooltip->move(pointLabel.x(),pointLabel.y() - m_tooltip->height()*1.5);
        m_tooltip->show();

        ui->frameIndexLabel->setText("Index:  " + QString::number(index));
        ui->frameBitrateLabel->setText("Bitrate:  " + sText);
        if (analyzeMode == FRAME_OF_BITRATE) ui->framePicttypeLabel->setText("Pict_type:  " + QString(PacketList[index].pict_type));
        if (analyzeMode == FRAME_OF_BITRATE) ui->framePktSizeLabel->setText("Pkt_size:  " + QString::number(PacketList[index].pkt_size));

    } else {
        m_tooltip->hide();
    }

}
