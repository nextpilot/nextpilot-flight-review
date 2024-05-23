/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include "plot_figure.hpp"

PlotFigure::PlotFigure(QWidget *parent) :
    ads::CDockManager(parent) {
    // ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);
    // ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    // ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);
    // ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
    // ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    // setStyleSheet("QWidget { background-color: #1e1e1e; }");
    // addAxes("");


    //    {
    //        // Create example content label - this can be any application specific
    //        // widget
    //        QLabel *l = new QLabel();
    //        l->setWordWrap(true);
    //        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    //        l->setText("Lorem ipsum dolor sit amet, consectetuer adipiscing elit. ");

    //        // Create a dock widget with the title Label 1 and set the created label
    //        // as the dock widget content
    //        ads::CDockWidget *DockWidget = new ads::CDockWidget("Label 1");
    //        DockWidget->setWidget(l);


    //        // Add the toggleViewAction of the dock widget to the menu to give
    //        // the user the possibility to show the dock widget if it has been closed
    //        //ui->menuView->addAction(DockWidget->toggleViewAction());

    //        // Add the dock widget to the top dock widget area
    //        _dock_manager->addDockWidget(ads::TopDockWidgetArea, DockWidget);
    //    }
}

PlotAxes *PlotFigure::addAxes(QString) {
    return nullptr;
}
