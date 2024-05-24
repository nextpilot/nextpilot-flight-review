/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/


#include <QMouseEvent>
#include <QPainter>
#include <QSignalMapper>
#include "tabbed_plot_widget.hpp"
#include "docked_figure.hpp"
#include "load_svg.hpp"

TabbedPlotWidget::TabbedPlotWidget(QWidget *parent) :
    QTabWidget(parent) {
    //
    setContentsMargins(0, 0, 0, 0);
    setTabsClosable(true);
    setMovable(true);

    tabBar()->installEventFilter(this);

    // rename tab name
    connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, &TabbedPlotWidget::on_tabBar_doubleClicked);
    // change current tab
    connect(this, &QTabWidget::currentChanged, this, &TabbedPlotWidget::on_tabWidget_currentChanged);


    // connect(this, &TabbedPlotWidget::destroyed, main_window, &MainWindow::on_tabbedAreaDestroyed);
    // connect(this, &TabbedPlotWidget::tabAdded, main_window, &MainWindow::onPlotTabAdded);
    // connect(this, &TabbedPlotWidget::undoableChange, main_window, &MainWindow::onUndoableChange);

    addTab("Tab1");
    addTab("Tab2");

    // 添加一个add new button
    {
        _button_add_new_tab = new QPushButton("", this);
        _button_add_new_tab->setFlat(true);
        _button_add_new_tab->setFixedSize(QSize(32, 32));
        _button_add_new_tab->setFocusPolicy(Qt::NoFocus);
        _button_add_new_tab->setIcon(LoadSvg(":/core/resources/svg/add_tab.svg"));
        connect(_button_add_new_tab, &QPushButton::pressed, this, &TabbedPlotWidget::on_addTabButton_pressed);
    }
}

TabbedPlotWidget::~TabbedPlotWidget() {
    QTabWidget::~QTabWidget();
}

DockedFigure *TabbedPlotWidget::addTab(QString name) {
    _tab_index_count++;
    if (name.isEmpty()) {
        name = QString("Tab%1").arg(_tab_index_count);
    }

    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);

    DockedFigure *page = new DockedFigure(this);

    QTabWidget::addTab(page, name);
    setCurrentWidget(page);

    // 修改tabBar的关闭按钮
    {
        QWidget     *button_widget = new QWidget(this);
        QHBoxLayout *layout        = new QHBoxLayout(button_widget);
        layout->setSpacing(2);
        layout->setMargin(0);

        QPushButton *close_button = new QPushButton(this);
        close_button->setIcon(LoadSvg(":/core/resources/svg/close-button.svg"));
        close_button->setFixedSize(QSize(16, 16));
        close_button->setFlat(true);

        connect(close_button, &QPushButton::pressed, this, [this]() {
            on_tabWidget_tabCloseRequested(tabBar()->currentIndex());
        });

        layout->addWidget(close_button);
        tabBar()->setTabButton(count() - 1, QTabBar::RightSide, button_widget);
    }


    return page;
}

bool TabbedPlotWidget::eventFilter(QObject *obj, QEvent *event) {
    if (obj == tabBar() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);

        int index = tabBar()->tabAt(mouse_event->pos());
        tabBar()->setCurrentIndex(index);

        if (mouse_event->button() == Qt::RightButton) {
        }
    }

    // Standard event processing
    return QObject::eventFilter(obj, event);
}

void TabbedPlotWidget::paintEvent(QPaintEvent *event) {
    QTabWidget::paintEvent(event);

    auto size = tabBar()->size();
    _button_add_new_tab->move(QPoint(size.width() + 5, 0));
}

void TabbedPlotWidget::on_addTabButton_pressed() {
    addTab(nullptr);
    // emit undoableChange();
}

void TabbedPlotWidget::on_tabBar_doubleClicked() {
    int idx = tabBar()->currentIndex();

    bool    ok = true;
    QString newName =
        QInputDialog::getText(this, tr("Change the tab name"), tr("New name:"),
                              QLineEdit::Normal, tabText(idx), &ok);
    if (ok) {
        setTabText(idx, newName);
        // currentWidget()->setName(newName);
    }
}

void TabbedPlotWidget::on_tabWidget_currentChanged(int index) {
    // 重绘当前页面
    DockedFigure *fig = dynamic_cast<DockedFigure *>(widget(index));
    if (fig) {
        //   fig->replot();
    }

    // 隐藏非当前tab的close button
    for (int i = 0; i < count(); i++) {
        auto button = tabBar()->tabButton(i, QTabBar::RightSide);
        if (button) {
            button->setHidden(i != index);
        }
    }
}

void TabbedPlotWidget::on_tabWidget_tabCloseRequested(int index) {
    auto *wdg = widget(index);
    if (wdg) {
        wdg->deleteLater();
    }
    removeTab(index);
}
