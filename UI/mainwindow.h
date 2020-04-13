#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QFileDialog>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLegend>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineSeries>
#include <QChartView>

#include "../source/ffmpegtool.h"

QT_CHARTS_USE_NAMESPACE

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

enum AnalyzeMode {
   BITRATE_MODE_NULL,
   GOP_OF_BITRATE,
   FRAME_OF_BITRATE,
   SECOND_OF_BITRATE
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void On_ClickedMenuActionGroup(QAction *action);
    void sltTooltip(bool, int, QBarSet *);

private:

    QString filename;               // load filename
    Ui::MainWindow *ui;
    QGridLayout *m_pLayout;           // playout var
    QChartView *m_chartView;          // chart view var
    QLabel*  m_tooltip = nullptr;  // mouse ptr for chart
    bool load_flag;
    StreamInfo info;
    vector<StreamFrames> PacketList;
    AnalyzeMode analyzeMode;
    void variable_init(void);
    void creat_bitrate_chart();
};
#endif // MAINWINDOW_H
