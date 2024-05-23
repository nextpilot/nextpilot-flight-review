#include "mainwindow.h"
#include "ui_mainwindow.h"
//#if !SARIBBON_USE_3RDPARTY_FRAMELESSHELPER
//#include "SAFramelessHelper.h"
//#endif
#include "SARibbonApplicationButton.h"
#include "SARibbonBar.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonCategory.h"
#include "SARibbonCheckBox.h"
#include "SARibbonColorToolButton.h"
#include "SARibbonComboBox.h"
#include "SARibbonCustomizeDialog.h"
#include "SARibbonCustomizeWidget.h"
#include "SARibbonGallery.h"
#include "SARibbonLineEdit.h"
#include "SARibbonMenu.h"
#include "SARibbonPannel.h"
#include "SARibbonQuickAccessBar.h"
#include "SARibbonToolButton.h"
#include "colorWidgets/SAColorGridWidget.h"
#include "colorWidgets/SAColorPaletteGridWidget.h"
#include "SARibbonSystemButtonBar.h"

#include "DockWidget.h"
#include "DockAreaWidget.h"
#include "DockAreaTitleBar.h"
#include "DockAreaTabBar.h"
#include "FloatingDockContainer.h"
#include "DockComponentsFactory.h"

MainWindow::MainWindow(QWidget *parent) :
    SARibbonMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setAcceptDrops(true);

    createRibbon();


    // CDockManager::setConfigFlag(CDockManager::OpaqueSplitterResize, true);
    // CDockManager::setConfigFlag(CDockManager::XmlCompressionEnabled, false);
    // CDockManager::setConfigFlag(CDockManager::FocusHighlighting, true);
    // CDockManager::setAutoHideConfigFlags(CDockManager::DefaultAutoHideConfig);

    // Create the dock manager after the ui is setup. Because the
    // parent parameter is a QMainWindow the dock manager registers
    // itself as the central widget as such the ui must be set up first.
    _dock_manager = new ads::CDockManager(this);


    // 绘图窗口(中心窗口)
    {
        _main_tabbed_plot      = new TabbedPlotWidget(this);
        ads::CDockWidget *dock = new ads::CDockWidget("Tabbed Plot");
        dock->setWidget(_main_tabbed_plot);
        _dock_manager->setCentralWidget(dock);
    }

    // 项目浏览器窗口
    {
        _project_explorer   = new ProjectExplorer(this);
        ads::CDockWidget *dock = new ads::CDockWidget("Project Explore");
        dock->setWidget(_project_explorer);
        dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromDockWidget);
        dock->resize(250, 150);
        dock->setMinimumSize(200, 150);
        _dock_manager->addDockWidget(ads::DockWidgetArea::LeftDockWidgetArea, dock);

        // ui->menuView->addAction(dock->toggleViewAction());
    }

    // 属性窗口


}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::createRibbon(){
   _ribbon_bar = ribbonBar();
    _ribbon_bar->applicationButton()->setText(tr("  HOME  "));
    //
    SARibbonCategory *main   = _ribbon_bar->addCategoryPage(tr("IMPORT"));
    SARibbonPannel   *pannel = main->addPannel(tr("actions"));
    pannel->addAction(tr("action1"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action2"), QIcon(":/app/icon/customize0.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action3"), QIcon(":/app/icon/save.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action4"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action5"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action6"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);

    // plots
    {
        SARibbonCategory *page   = _ribbon_bar->addCategoryPage(tr("PLOTS"));
        SARibbonPannel    pannel = page->addPannel(tr("OPTIONS"));
        // x grid
        // y grid
        // link x axis
        // x axis unit
        // show legend
    }
    // Data
    //SARibbonCategory* data = bar->addCategoryPage(tr("Data"));

    //

}
