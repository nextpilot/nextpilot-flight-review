/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __PROJECT_EXPLORER_H__
#define __PROJECT_EXPLORER_H__

#include <QWidget>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeView>
#include <QMenu>

class ProjectExplorer : public QWidget {
    Q_OBJECT

public:
    explicit ProjectExplorer(QWidget *parent = nullptr);
    ~ProjectExplorer() override;

private:
    //    void createActions();
    //    void contextMenuEvent(QContextMenuEvent *) override;
    //    bool eventFilter(QObject *, QEvent *) override;
    //    void keyPressEvent(QKeyEvent *) override;
    //    void collapseParents(const QModelIndex &, const QList<QModelIndex> &expanded);
    //    bool filter(const QModelIndex &, const QString &);
    //    void showErrorMessage(const QString &);

private:
    //
    QLineEdit   *m_leFilter{nullptr};
    QPushButton *bFilterOptions{nullptr};

    QTreeView *m_treeView{nullptr};
    QFrame    *m_frameFilter{nullptr};
};
#endif // __PROJECT_EXPLORER_H__
