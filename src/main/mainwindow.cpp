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
    SARibbonMainWindow(parent),
    // ads::CDockManager(this),
    ui(new Ui::MainWindow) {
    setAcceptDrops(true);

    // 创建ui界面
    ui->setupUi(this);

    // 创建ribbon菜单
    createRibbon();

    // 项目浏览器窗口
    createPrjectExplorer();

    // 绘图窗口(中心窗口)
    createTabbedPlot();

    // 属性窗口
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::createRibbon() {
    _ribbon_bar = ribbonBar();
    _ribbon_bar->applicationButton()->setText(tr("  HOME  "));
    //
    SARibbonCategory *main   = _ribbon_bar->addCategoryPage(tr("IMPORT"));
    SARibbonPannel   *pannel = main->addPannel(tr("actions"));
    pannel->addAction(tr("action1"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup);
    // pannel->addAction(tr("action2"), QIcon(":/app/icon/customize0.svg"), QToolButton::InstantPopup);
    // pannel->addAction(tr("action3"), QIcon(":/app/icon/save.svg"), QToolButton::InstantPopup);
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

void MainWindow::createDockManager() {
    // 配置docker
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, true);
    // ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);

    // must be after the config flags!
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
    ads::CDockManager::setAutoHideConfigFlag(ads::CDockManager::AutoHideShowOnMouseOver, true);


    // Create the dock manager after the ui is setup. Because the
    // parent parameter is a QMainWindow the dock manager registers
    // itself as the central widget as such the ui must be set up first.
    if (!_dock_manager) {
        _dock_manager = new ads::CDockManager(this);
    }
}

void MainWindow::createPrjectExplorer() {
    createDockManager();
    _project_explorer      = new ProjectExplorer(this);
    ads::CDockWidget *dock = new ads::CDockWidget("Project Explore");
    dock->setWidget(_project_explorer);
    dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromDockWidget);
    dock->resize(250, 150);
    dock->setMinimumSize(200, 150);
    _dock_manager->addDockWidget(ads::DockWidgetArea::LeftDockWidgetArea, dock);

    // ui->menuView->addAction(dock->toggleViewAction());
}

void MainWindow::createTabbedPlot() {
    createDockManager();
    _main_tabbed_plot      = new TabbedPlotWidget(this);
    ads::CDockWidget *dock = new ads::CDockWidget("Tabbed Plot");
    dock->setWidget(_main_tabbed_plot);
    _dock_manager->addDockWidget(ads::RightDockWidgetArea, dock);
    //_dock_manager->setCentralWidget(dock);

    //    auto* wdg = new QPushButton(this);
    //    dock->setWidget(wdg);
    //   _dock_manager->addDockWidget(ads::CenterDockWidgetArea,dock);
}
