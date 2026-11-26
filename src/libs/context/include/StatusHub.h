#pragma once

#include "Contribution.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

namespace nextpilot::plotscope::context
{

// 状态栏运行态登记处：声明（StatusSpec）+ 常驻文案 + 瞬时消息。
//
// 为什么放在 AppContext 而不是壳里（D21 教训）：
//   模块/视图要把"数据源条数、游标读数、回放状态"推到状态栏，但模块不能反向依赖
//   ps_gui。于是推送走这里的纯数据接缝——生产方只调 updateStatus/flash，
//   外壳订阅 statusChanged/statusFlashed 渲染；模块既不认识 QWidget，也不认识壳。
//
// 职责边界（重要）：
//   - 本类只管"声明 + 文案 + 排序"，**不管节流**。某个键要不要限频是**生产方**的
//     领域知识（例如游标 10Hz 属 plot 域，见 D2），壳与 hub 都不该认识业务键。
//   - 平滑/闪烁动画、倒计时样式归壳；本类只发事件。
//
// 铁律与 AppContext 一致：不含 QWidget；文案全为可序列化字符串；实例归 AppContext，
// 不做全局单例，测试之间互不污染。
class StatusHub;

// A8b-M3-a 3a-3：运行层动态条目（vscode.StatusBarItem 对位）。
// 模块/扩展不直接碰 StatusSpec 声明表，经 StatusHub::createStatusBarItem 拿句柄：
// show() = 以 id/name/alignment/priority 登记声明 + 推文案；hide() = 撤显示（可再 show）；
// dispose() = 撤显示并作废（幂等，作废后 show 拒）。文案变更 setText → updateStatus。
class StatusBarItem : public QObject
{
    Q_OBJECT
public:
    const QString id;                 // 动态生成（"dynamic.N"），不与声明键冲突
    QString name;                     // 悬浮名（vscode.StatusBarItem.name）
    QString text;                     // 当前文案
    QString tooltip;
    QString command;                  // 点击触发的命令 id
    registry::StatusSide alignment = registry::StatusSide::Right;
    int priority = 0;

    bool visible() const { return m_visible; }
    bool disposed() const { return m_disposed; }

    void show();
    void hide();
    void dispose(); // 幂等
    void setText(const QString &t);          // 显示中直接 updateStatus
    void setTooltip(const QString &tip);     // 声明期字段：显示中变更需 hide/show 生效
    void setCommand(const QString &cmd);     // 同上

private:
    friend class StatusHub;
    StatusBarItem(const QString &itemId, StatusHub *hub);
    StatusHub *m_hub = nullptr;
    bool m_visible = false;
    bool m_disposed = false;
};

class StatusHub : public QObject
{
    Q_OBJECT
public:
    explicit StatusHub(QObject *parent = nullptr);

    // 登记一个状态栏槽位。同 id 先声明者优先（与 UiSlotHub 同口径）。
    void registerSpec(const registry::StatusSpec &spec);

    // A8b-M3-a 3a-3：vscode.window.createStatusBarItem(alignment, priority) 对位。
    // 句柄由 hub 拥有（dispose 后失效），id 自动生成保证唯一；owner 语义归调用方记录。
    StatusBarItem *createStatusBarItem(registry::StatusSide alignment, int priority = 0);

    // 撤销声明（hide/dispose 的底层）：未声明静默；文案一并撤。
    void unregisterSpec(const QString &id);

    // 已登记槽位；side 为空返回全部。同侧按 priority 升序、再按 id 稳定排序。
    QList<registry::StatusSpec> specs() const;
    QList<registry::StatusSpec> specsOf(registry::StatusSide side) const;
    // 注意：返回内部存储指针，**同一次 registerSpec 之后即失效**（QHash 可能重排）。
    // 需要跨调用持有时用 specs()/specsOf() 拷值。
    const registry::StatusSpec *spec(const QString &id) const;

    // 推送常驻文案：未声明的 id 忽略（避免模块推野键污染状态栏）；文本未变不发信号。
    void updateStatus(const QString &id, const QString &text);

    // 当前常驻文案（未声明返回空串）。
    QString text(const QString &id) const;

    // 瞬时消息（原 QStatusBar::showMessage 的契约化入口）：壳体按 durationMs 自行淡出。
    void flash(const QString &text, int durationMs = 3000);

    void clear();

Q_SIGNALS:
    void statusChanged(const QString &id, const QString &text);
    void statusSpecDeclared(const nextpilot::plotscope::registry::StatusSpec &spec);
    void statusFlashed(const QString &text);

private:
    QHash<QString, registry::StatusSpec> m_specs;
    QHash<QString, QString> m_text;
    QList<StatusBarItem *> m_dynamicItems; // hub 拥有，clear/dispose 释放
    int m_dynamicCounter = 0;
};

} // namespace nextpilot::plotscope::context
