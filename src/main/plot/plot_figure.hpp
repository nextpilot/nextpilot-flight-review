/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __PLOT_FIGURE_H__
#define __PLOT_FIGURE_H__

#include <QWidget>
#include <QDomElement>
#include <QXmlStreamReader>
#include "DockManager.h"
#include "plot_axes.hpp"

class PlotFigure : public ads::CDockManager {
    Q_OBJECT

public:
    PlotFigure(QWidget *parent);
    ~PlotFigure() override;

    PlotAxes *addAxes(QString name);
    PlotAxes *addSubplot();
};
#endif // __PLOT_FIGURE_H__
