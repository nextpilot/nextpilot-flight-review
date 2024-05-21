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

MainWindow::MainWindow(QWidget *parent)
    : SARibbonMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    SARibbonBar* bar = ribbonBar();
    bar->applicationButton()->setText(tr("  HOME  "));
    //
    SARibbonCategory* main = bar->addCategoryPage(tr("IMPORT"));
    SARibbonPannel* pannel     = main->addPannel(tr("actions"));
    pannel->addAction(tr("action1"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action2"), QIcon(":/app/icon/customize0.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action3"), QIcon(":/app/icon/save.svg"), QToolButton::InstantPopup);
    pannel->addAction(tr("action4"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action5"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);
    pannel->addAction(tr("action6"), QIcon(":/core/resources/svg/grid.svg"), QToolButton::InstantPopup, SARibbonPannelItem::Small);


    // plots
    {
        SARibbonCategory* page = bar->addCategoryPage(tr("PLOTS"));
        SARibbonPannel pannel = page->addPannel(tr("OPTIONS"));
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

MainWindow::~MainWindow()
{
    delete ui;
}

