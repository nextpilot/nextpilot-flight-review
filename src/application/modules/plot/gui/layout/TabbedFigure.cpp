#include "TabbedFigure.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QTabBar>
#include <QWidget>

#include <algorithm>

namespace nextpilot::plotscope::gui
{

TabbedFigure::TabbedFigure(QWidget *parent) : QTabWidget(parent)
{
    setDocumentMode(true);
    // 页序 = 绘图页序，拖拽改序会让页索引与页对象错位，故不可拖。
    setMovable(false);
    setTabsClosable(true);
    setUsesScrollButtons(true);
    tabBar()->setExpanding(false);
    tabBar()->setElideMode(Qt::ElideRight);

    // 末尾「＋」：点一下＝加一页，不显示关闭按钮。
    // 建它时屏蔽 currentChanged：count 0→1 会把 currentIndex 从 -1 变成 0。
    blockSignals(true);
    const int plus = QTabWidget::addTab(new QWidget(this), QStringLiteral("＋"));
    tabBar()->setTabToolTip(plus, QStringLiteral("新建页"));
    tabBar()->setTabButton(plus, QTabBar::RightSide, nullptr);
    blockSignals(false);

    QObject::connect(this, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0)
            return;
        if (index >= pageCount())
        {
            Q_EMIT addPageRequested(); // 点到「＋」
            return;
        }
        Q_EMIT pageSelected(index);
    });
    QObject::connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index >= 0 && index < pageCount())
            Q_EMIT pageCloseRequested(index);
    });
    // 双击页签改名（「＋」不参与）。
    QObject::connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0 || index >= pageCount())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名页签"),
                                                   QStringLiteral("页名"), QLineEdit::Normal,
                                                   tabText(index), &ok);
        const QString trimmed = name.trimmed();
        if (!ok || trimmed.isEmpty())
            return;
        setTabText(index, trimmed);
        Q_EMIT titleEdited(index, trimmed);
    });
}

int TabbedFigure::plusIndex() const
{
    return std::max(0, count() - 1);
}

int TabbedFigure::insertPage(QWidget *host, const QString &title)
{
    const int index = QTabWidget::insertTab(plusIndex(), host, title);
    tabBar()->setTabToolTip(index, QStringLiteral("绘图页：双击页签可重命名"));
    return index;
}

void TabbedFigure::removePageAt(int index)
{
    if (index < 0 || index >= pageCount())
        return;
    // 移除期间的索引抖动不该被当成"用户选页"。
    blockSignals(true);
    removeTab(index);
    blockSignals(false);
}

int TabbedFigure::pageCount() const
{
    return std::max(0, count() - 1);
}

int TabbedFigure::currentPage() const
{
    return currentIndex();
}

void TabbedFigure::setCurrentPage(int index)
{
    setCurrentIndex(index);
}

void TabbedFigure::setPageTitle(int index, const QString &title)
{
    if (index >= 0 && index < pageCount())
        setTabText(index, title);
}

} // namespace nextpilot::plotscope::gui
