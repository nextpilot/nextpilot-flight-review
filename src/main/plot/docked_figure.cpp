/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include "docked_figure.hpp"
#include "DockComponentsFactory.h"
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"

class SplittableComponentsFactory : public ads::CDockComponentsFactory {
public:
    ads::CDockAreaTitleBar *createDockAreaTitleBar(ads::CDockAreaWidget *dock_area) const override {
        auto title_bar = new ads::CDockAreaTitleBar(dock_area);
        title_bar->setVisible(false);
        return title_bar;
    }
};

DockedFigure::DockedFigure(QWidget *parent) :
    ads::CDockManager(parent) {
    ads::CDockComponentsFactory::setFactory(new SplittableComponentsFactory());

    // 先添加2个坐标系
    addAxes("axes 1");
    addAxes("axes 2");
}

DockedFigure::~DockedFigure() {
}

DockedAxes *DockedFigure::addAxes(QString title) {
    DockedAxes *axes = new DockedAxes(title, this);
    auto       *area = addDockWidget(ads::TopDockWidgetArea, axes);
    area->setAllowedAreas(ads::OuterDockAreas);

    return axes;
}
