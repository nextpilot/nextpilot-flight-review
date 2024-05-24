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
#include "docked_figure.hpp"

class TabbedPlotWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit TabbedPlotWidget(QWidget *parent = nullptr);
    ~TabbedPlotWidget() override;

    DockedFigure *addTab(QString name);
    // DockedFigure *addTab(Widget *page, const QString &label);

protected:
    virtual bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void on_tabBar_doubleClicked();
    void on_tabWidget_currentChanged(int index);
    void on_tabWidget_tabCloseRequested(int index);
    void on_addTabButton_pressed();
    void paintEvent(QPaintEvent *event) override;

private:
    QPushButton *_button_add_new_tab;

    int _tab_index_count = 0;
};

#endif // __TABBED_PLOT_WIDGET_H__
