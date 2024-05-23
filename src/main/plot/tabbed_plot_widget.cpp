/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/


#include "tabbed_plot_widget.hpp"
#include "plot_figure.hpp"

TabbedPlotWidget::TabbedPlotWidget(QWidget *parent) :
    QTabWidget(parent) {
    //
    setContentsMargins(0, 0, 0, 0);
    setTabsClosable(true);
    setMovable(true);

    tabBar()->installEventFilter(this);

    // rename tab name
    //connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, &TabbedPlotWidget::renameCurrentTab);
    // change current tab
    //connect(this, &QTabWidget::currentChanged, this, &TabbedPlotWidget::on_tabWidget_currentChanged);


    // connect(this, &TabbedPlotWidget::destroyed, main_window, &MainWindow::on_tabbedAreaDestroyed);
    // connect(this, &TabbedPlotWidget::tabAdded, main_window, &MainWindow::onPlotTabAdded);
    // connect(this, &TabbedPlotWidget::undoableChange, main_window, &MainWindow::onUndoableChange);

    addTab("Tab 1");
    addTab("Tab 2");

    // _button_add_new_tab = new QPushButton("+", this);
    // _button_add_new_tab->setFlat(true);
    // _button_add_new_tab->setFixedSize(QSize(32, 32));
    // _button_add_new_tab->setFocusPolicy(Qt::NoFocus);
}

TabbedPlotWidget::~TabbedPlotWidget() {
    QTabWidget::~QTabWidget();
}

PlotFigure *TabbedPlotWidget::addTab(QString name) {
    PlotFigure *page = new PlotFigure(this);
    QTabWidget::addTab(page, name);

    setCurrentWidget(page);

    return page;
}
