#pragma once

#include "UiSlotRegistry.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QStackedWidget;
class QToolButton;

// Ribbon 栏：自研控件（Qt 无原生 ribbon）。
//
// 结构：页签（RibbonPage）-> 分组（RibbonGroup）-> 动作（UiAction）。
// 页签与分组来自 core 的插槽登记表（SlotKind::RibbonPage / RibbonGroup），
// 动作来自插槽里绑定的 UiAction（工具箱贡献或器官本地登记）。
//
// 职责单一：只负责"按插槽数据渲染出 ribbon 并发出点击"，不关心业务、
// 不关心数据、不认识任何具体领域。绘制用 QPainter，无第三方依赖。
namespace ng::gui
{

class RibbonBar : public QWidget
{
    Q_OBJECT
public:
    explicit RibbonBar(QWidget *parent = nullptr);

    // 按插槽登记表重建整个 ribbon。数据驱动，改插槽即改外观。
    void rebuild(const ng::core::UiSlotRegistry &slotReg);

    // 供测试与诊断：当前页签数、某页签的分组数、某分组的动作数。
    int pageCount() const;
    int groupCount(const QString &pageArea) const;
    int actionCount(const QString &groupArea) const;
    QStringList pageAreas() const;

signals:
    // 点击某个动作时发出（携带动作 id，供上层执行意图）。
    void actionTriggered(const QString &actionId);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // 一个分组对应一个容器部件（大按钮 + 小按钮列）。
    struct Group
    {
        QString area;
        QString label;
        QList<QString> actionIds;
        QList<QString> actionLabels;
        QWidget *container = nullptr;
    };

    struct Page
    {
        QString area;
        QString label;
        QList<Group> groups;
    };

    // 页签行（横向排布的页签按钮）与页面栈。
    QWidget *m_tabRow = nullptr;
    QHBoxLayout *m_tabLay = nullptr;
    QStackedWidget *m_stack = nullptr;

    QList<Page> m_pages;
    int m_currentPage = 0;
    QMap<QString, int> m_pageIndexByArea;

    // 点击 -> 动作 id 的映射（按钮的 objectName 存动作 id）。
    void onButtonClicked();
};

} // namespace ng::gui
