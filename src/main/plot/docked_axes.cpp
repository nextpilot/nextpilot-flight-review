/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include "docked_axes.hpp"

DockedAxes::DockedAxes(QWidget *parent) :
    DockedAxes("plot", parent) {
}

DockedAxes::DockedAxes(const QString &title, QWidget *parent) :
    ads::CDockWidget(title, parent) {
    setFrameShape(QFrame::NoFrame);
    setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    // 添加qwt坐标轴
    _qwt_plot = new QwtPlot(title, this);
    setWidget(_qwt_plot);
}
