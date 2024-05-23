#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "SARibbonMainWindow.h"
#include "DockManager.h"
#include "plot/tabbed_plot_widget.hpp"
#include "project_explorer.hpp"

QT_BEGIN_NAMESPACE

namespace Ui {
class MainWindow;
}

QT_END_NAMESPACE

class MainWindow : public SARibbonMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();


    void createRibbon();
private:
    Ui::MainWindow *ui;

    // ribbon工具栏
    SARibbonBar* _ribbon_bar;

    // ads管理器
    ads::CDockManager *_dock_manager;
    // 项目浏览器
    ProjectExplorer* _project_explorer;
    // 绘图窗口
    TabbedPlotWidget *_main_tabbed_plot;
    // 属性窗口
};
#endif // MAINWINDOW_H
