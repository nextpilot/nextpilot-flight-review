#include "ViewState.h"

#include <cmath>

namespace nextpilot::plotscope::gui
{

namespace
{
bool sameValue(double a, double b)
{
    return std::fabs(a - b) < 1e-9;
}
} // namespace

bool ViewState::Range::equals(const Range &o) const
{
    return sameValue(xLo, o.xLo) && sameValue(xHi, o.xHi) && sameValue(yLo, o.yLo)
           && sameValue(yHi, o.yHi);
}

ViewState::ViewState(QObject *parent) : QObject(parent) {}

void ViewState::resetBaseline(const Range &r)
{
    m_baseline = r;
}

void ViewState::record(const Range &current)
{
    // 撤销恢复期间与"没变化"都不入栈。
    if (m_restoring || m_baseline.equals(current))
    {
        m_baseline = current;
        return;
    }
    m_history.push_back(m_baseline);
    if (m_history.size() > 64)
        m_history.removeFirst();
    m_baseline = current;
}

void ViewState::setBaseline(const Range &r)
{
    m_baseline = r;
}

bool ViewState::canUndo() const
{
    return !m_history.isEmpty();
}

bool ViewState::takeUndo(Range *out)
{
    if (m_history.isEmpty())
        return false;
    if (out)
        *out = m_history.takeLast();
    return true;
}

int ViewState::historySize() const
{
    return m_history.size();
}

bool ViewState::restoring() const
{
    return m_restoring;
}

void ViewState::setRestoring(bool on)
{
    m_restoring = on;
}

QString ViewState::timeFormat() const
{
    return m_timeFormat;
}

void ViewState::setTimeFormat(const QString &mode)
{
    const QString next = (mode == QStringLiteral("hms") || mode == QStringLiteral("utc"))
                             ? mode
                             : QStringLiteral("relative");
    if (next == m_timeFormat)
        return;
    m_timeFormat = next;
    Q_EMIT timeFormatChanged(m_timeFormat);
}

QString ViewState::titleOverride() const
{
    return m_titleOverride;
}

void ViewState::setTitleOverride(const QString &title)
{
    m_titleOverride = title.trimmed();
    Q_EMIT titleOverrideChanged(m_titleOverride);
}

QString ViewState::plotKind() const
{
    return m_plotKind;
}

void ViewState::setPlotKind(const QString &kind)
{
    m_plotKind = kind;
}

void ViewState::setAxisTransform(const QString &axis, const QString &descriptorId)
{
    if (descriptorId.isEmpty())
        m_axisTransforms.remove(axis);
    else
        m_axisTransforms.insert(axis, descriptorId);
}

QString ViewState::axisTransform(const QString &axis) const
{
    return m_axisTransforms.value(axis);
}

void ViewState::clearAxisTransforms()
{
    m_axisTransforms.clear();
}

void ViewState::replaceAxisTransforms(const QHash<QString, QString> &map)
{
    m_axisTransforms = map;
}

} // namespace nextpilot::plotscope::gui
