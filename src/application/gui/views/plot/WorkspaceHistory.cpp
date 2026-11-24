#include "WorkspaceHistory.h"

#include "PlotWorkspace.h"

// 文档 spec 结构在模块层；与视图层的 layout/PlotLayout.h 同名不同物，
// 靠路径区分：本文件读写的是"可存盘的布局声明"，不是 dock 布局容器。
#include "../../../modules/plot/PlotDocument.h"

namespace nextpilot::plotscope::gui
{

WorkspaceHistory::WorkspaceHistory(PlotWorkspace *ws, QObject *parent)
    : QObject(parent), m_ws(ws)
{
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(kQuietMs);
    QObject::connect(&m_quiet, &QTimer::timeout, this, &WorkspaceHistory::settle);
}

void WorkspaceHistory::notifyMutation()
{
    if (m_restoring)
        return;
    m_dirty = true;
    m_quiet.start();
}

void WorkspaceHistory::setRestoring(bool on)
{
    m_restoring = on;
    if (on)
    {
        m_quiet.stop();
        m_dirty = false;
    }
}

void WorkspaceHistory::noteRestored()
{
    m_quiet.stop();
    m_dirty = false;
    m_last = snapshot();
}

void WorkspaceHistory::settle()
{
    m_dirty = false;
    const QJsonObject cur = snapshot();
    if (m_last.isEmpty())
    {
        m_last = cur; // 首个基线：没有可回退的更早状态
        return;
    }
    if (cur == m_last)
        return; // 相同快照丢弃（数据喂养等非结构噪声）
    if (m_undo.size() >= kMaxSteps)
        m_undo.removeFirst();
    m_undo.append(m_last);
    m_last = cur;
    Q_EMIT changed();
}

bool WorkspaceHistory::canUndo() const
{
    if (!m_undo.isEmpty())
        return true;
    if (!m_dirty || m_last.isEmpty())
        return false;
    return snapshot() != m_last;
}

bool WorkspaceHistory::undo()
{
    if (m_quiet.isActive())
        flushPending(); // 未决手势先结算（连点 undo 时立即生效）
    if (m_undo.isEmpty())
        return false;
    const QJsonObject prev = m_undo.takeLast();
    const auto spec = plot::PlotDocumentSpec::fromJson(prev);
    m_restoring = true;
    const bool ok = m_ws->applyDocumentSpec(spec, m_ws->sources());
    m_restoring = false;
    m_last = snapshot();
    Q_EMIT changed();
    return ok;
}

void WorkspaceHistory::flushPending()
{
    m_quiet.stop();
    if (!m_dirty)
        return;
    settle();
}

QJsonObject WorkspaceHistory::snapshot() const
{
    return m_ws->toDocumentSpec().toJson();
}

} // namespace nextpilot::plotscope::gui
