/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include "project_explorer.hpp"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

ProjectExplorer::ProjectExplorer(QWidget *parent) :
    QWidget(parent) {
    // 垂直布局
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    // 添加搜索框
    {
        auto *m_frameFilter = new QFrame(this);

        // 水平布局
        auto *layoutFilter = new QHBoxLayout(m_frameFilter);
        layoutFilter->setSpacing(0);
        layoutFilter->setContentsMargins(0, 0, 0, 0);
        // 搜索框
        auto *m_leFilter = new QLineEdit(m_frameFilter);
        m_leFilter->setClearButtonEnabled(true);
        m_leFilter->setPlaceholderText(tr("Search/Filter"));
        layoutFilter->addWidget(m_leFilter);
        // 搜索选项
        auto *bFilterOptions = new QPushButton(m_frameFilter);
        bFilterOptions->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
        bFilterOptions->setCheckable(true);
        bFilterOptions->setFlat(true);
        layoutFilter->addWidget(bFilterOptions);

        layout->addWidget(m_frameFilter);
    }

    // 对象树
    {
        auto *m_treeView = new QTreeView(parent);

        m_treeView->setAnimated(true);
        m_treeView->setAlternatingRowColors(true);
        m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_treeView->setUniformRowHeights(true);
        m_treeView->viewport()->installEventFilter(this);
        m_treeView->header()->setStretchLastSection(true);
        m_treeView->header()->installEventFilter(this);
        m_treeView->setDragEnabled(true);
        m_treeView->setAcceptDrops(true);
        m_treeView->setDropIndicatorShown(true);
        m_treeView->setDragDropMode(QAbstractItemView::InternalMove);

        layout->addWidget(m_treeView);
    }
}

ProjectExplorer::~ProjectExplorer() {
}
