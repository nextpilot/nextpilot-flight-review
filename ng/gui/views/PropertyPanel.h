#pragma once

#include <QWidget>

#include "Selection.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace nextpilot::plotscope::core
{
class AppContext;
class IIntentExecutor;
}

namespace nextpilot::plotscope::gui
{

// 通用属性面板：dock.right 的唯一内容（替换原 CAN 面板的默认占用）。
//
// 它是 PropertySchema 契约的**纯视图**：
//   · 不知道任何业务类型，只按 schema 画对象树 + 属性行；
//   · 订阅 AppContext::selectionChanged -> 按 kind 找 provider 拿 schema；
//   · 编辑一行时发 prop.* view action（由各视图 provider 处理），
//     与按钮/脚本共用同一条意图路径。
//
// 为什么能通用：老代码 plotting 属性页与 protocol 编辑器各自依赖胖
// PropertyManager；ng 先定 PropertySchema 契约（core/headless 可见），
// 于是两份私有属性面板收敛成这一个。选中什么、显示什么，全由
// Selection + PropertyProvider 决定，面板本身零业务知识。
class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(nextpilot::plotscope::core::AppContext &appCtx,
                           nextpilot::plotscope::core::IIntentExecutor &executor,
                           QWidget *parent = nullptr);
    ~PropertyPanel() override;

    // 诊断/测试用。
    QString schemaTitle() const;
    int objectCount() const;
    int rowCount() const;
    QString selectionKindText() const;

public Q_SLOTS:
    // 供外部/测试直接触发刷新（正常由 selectionChanged 自动驱动）。
    void refreshFromSelection();

private Q_SLOTS:
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    void showEmpty(const QString &message);
    void rebuild(const QJsonObject &schemaJson);
    QString selLabelFallback() const;
    // 提交属性编辑：优先走 view action，失败再走 intent prop.set。
    void commitEdit(const QString &objectId, const QString &key, const QString &value);

    nextpilot::plotscope::core::AppContext &m_appCtx;
    nextpilot::plotscope::core::IIntentExecutor &m_executor;
    QLabel *m_header = nullptr;
    QLabel *m_empty = nullptr;
    QTreeWidget *m_tree = nullptr;
    bool m_rebuilding = false; // 重建期间屏蔽 itemChanged 回环
};

} // namespace nextpilot::plotscope::gui
