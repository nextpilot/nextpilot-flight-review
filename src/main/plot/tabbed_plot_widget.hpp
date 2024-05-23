/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __TABBED_PLOT_WIDGET_H__
#define __TABBED_PLOT_WIDGET_H__

#include <QWidget>
#include <QTabWidget>
#include <QMainWindow>
#include <QTableWidget>
#include <QDomDocument>
#include <QPushButton>
#include "plot_figure.hpp"

class TabbedPlotWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit TabbedPlotWidget(QWidget *parent = nullptr);
    ~TabbedPlotWidget() override;

    PlotFigure *addTab(QString name);
    // PlotFigure *addTab(Widget *page, const QString &label);

private:
    QPushButton *_button_add_new_tab;
};

#endif // __TABBED_PLOT_WIDGET_H__
