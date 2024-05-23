/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __PLOT_AXES_H__
#define __PLOT_AXES_H__

#include <QWidget>

#include "DockWidget.h"

#include "qwt_axis.h"
#include "qwt_legend.h"
#include "qwt_plot.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_grid.h"
#include "qwt_plot_canvas.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_opengl_canvas.h"
#include "qwt_plot_rescaler.h"
#include "qwt_plot_legenditem.h"
#include "qwt_plot_marker.h"
#include "qwt_plot_layout.h"
#include "qwt_scale_engine.h"
#include "qwt_scale_map.h"
#include "qwt_scale_draw.h"
#include "qwt_scale_widget.h"
#include "qwt_symbol.h"
#include "qwt_text.h"

class PlotAxes : public ads::CDockWidget {
    Q_OBJECT

public:
    PlotAxes(QWidget *parent = nullptr);
    PlotAxes(const QString &title, QWidget *parent = nullptr);


private:
    QwtPlot *_qwt_plot;
    // QwtPlotOpenGLCanvas *_qwt_canvas;
};

#endif // __PLOT_AXES_H__
