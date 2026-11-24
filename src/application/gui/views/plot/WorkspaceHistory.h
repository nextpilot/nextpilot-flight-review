#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace nextpilot::plotscope::gui
{

class PlotWorkspace;

// 全工作区快照 undo 栈（第3仗 3b，#IKXD1P）。
//
// 定性：体验等效于 PJ4 的全工作区快照模型，不照搬其实现——
//   - 快照 = PlotWorkspace::toDocumentSpec()（页结构 + 各格曲线/样式/视图状态），
//     复用既有恢复链 applyDocumentSpec，不自建第二通道；
//   - 任何会改变工作区形状的变更路径调 notifyMutation()（变更发生后调用）；
//   - 连续手势安静 200ms 后结算一步：把"手势前"的基线快照入栈
//     （等体验：连续滚轮/连续显隐切换 = 1 步 undo）；
//   - 结算时与基线相同的快照丢弃（数据喂养等非结构噪声不入栈）；
//   - 恢复期（applyDocumentSpec / 布局载入）notifyMutation 无效，防误发布；
//   - 栈上限 64 步，FIFO 淘汰最旧。
//
// 覆盖度（按本项目实际架构裁剪）：曲线增删/显隐/样式/挂轴（经 SeriesStore::changed
// 转发）+ 拆分/删格/页增删复制/图型切换。视口 zoom/pan 仍走画布局部
// ViewState::m_history（plot.view.undo，第 #202 条既有体验），两者不混栈。
class WorkspaceHistory : public QObject
{
    Q_OBJECT // NOLINT
public:
    static constexpr int kMaxSteps = 64;
    static constexpr int kQuietMs = 200; // 手势安静判定

    explicit WorkspaceHistory(PlotWorkspace *ws, QObject *parent = nullptr);

    // 动作流入口：变更发生后调用（信号转发或结构方法起始处均可）。
    // 只标脏并重置 200ms 安静计时；真正结算见 flushPending()。
    void notifyMutation();

    // 恢复期防误发布：布局载入 / undo 自身恢复期间忽略动作流。
    void setRestoring(bool on);
    // 恢复链结束调用：基线对齐当前状态、清挂起（undo 栈保留）。
    void noteRestored();

    bool canUndo() const;
    int stepCount() const { return m_undo.size(); }
    // 立即结算未决手势，然后弹栈恢复一步。返回是否成功。
    bool undo();

    // 测试与调试：立即结算挂起手势（等价安静 200ms 到点）。
    void flushPending();
    QJsonObject baselineForTest() const { return m_last; }

signals:
    void changed(); // 栈深变化（按钮使能/计数刷新用）

private:
    QJsonObject snapshot() const;
    void settle(); // 结算：基线为空则立基线；不同则入栈并前移基线；相同丢弃

    PlotWorkspace *m_ws = nullptr;
    QTimer m_quiet;
    QVector<QJsonObject> m_undo; // 只存"变更前"快照
    QJsonObject m_last;          // 基线：最后一次入栈/恢复时的全工作区快照
    bool m_dirty = false;        // 安静期内有未结算变更
    bool m_restoring = false;
};

} // namespace nextpilot::plotscope::gui
