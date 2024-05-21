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

MainWindow::MainWindow(QWidget *parent) :
    SARibbonMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    {
        // Create the dock manager after the ui is setup. Because the
        // parent parameter is a QMainWindow the dock manager registers
        // itself as the central widget as such the ui must be set up first.
        _dock_manager = new ads::CDockManager(this);

        // Create example content label - this can be any application specific
        // widget
        QLabel *l = new QLabel();
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        l->setText("Lorem ipsum dolor sit amet, consectetuer adipiscing elit. ");

        // Create a dock widget with the title Label 1 and set the created label
        // as the dock widget content
        ads::CDockWidget *DockWidget = new ads::CDockWidget("Label 1");
        DockWidget->setWidget(l);


        // Add the toggleViewAction of the dock widget to the menu to give
        // the user the possibility to show the dock widget if it has been closed
        //ui->menuView->addAction(DockWidget->toggleViewAction());

        // Add the dock widget to the top dock widget area
        _dock_manager->addDockWidget(ads::TopDockWidgetArea, DockWidget);
    }

    SARibbonBar *bar = ribbonBar();
    bar->applicationButton()->setText(tr("  HOME  "));
    //
    SARibbonCategory *main   = bar->addCategoryPage(tr("IMPORT"));
    SARibbonPannel   *pannel = main->addPannel(tr("actions"));
    pannel->addAction(tr("action1"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action2"), QIcon(":/app/icon/customize0.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action3"), QIcon(":/app/icon/save.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action4"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action5"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action6"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);


    // plots
    {
        SARibbonCategory *page   = bar->addCategoryPage(tr("PLOTS"));
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

MainWindow::~MainWindow() {
    delete ui;
}
