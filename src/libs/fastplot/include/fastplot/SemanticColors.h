#pragma once

#include <QColor>
#include <QHash>
#include <QString>

namespace nextpilot::plotscope::fastplot
{

// 语义色接缝（验收 8）：库只认 id -> 实色 的纯数据映射，不认识主题 token 表。
// 上层从 ThemeManager 把模块声明的语义 token 解析成实色后推下来；绘制点按 id 取色，
// 取不到就用调用点的历史兜底色——于是"未注入 token"与"接管前"像素完全一致，
// 接管 token 不会静默改变外观。
//
// 为什么放 libs：这是对下层绘图库的被动注入（上层推、库只读），库不依赖 gui/registry，
// 符合分层；隐藏运行时状态仍归上层实例（成员对象，非命名空间全局）。
class SemanticColors
{
public:
    void set(const QString &id, const QColor &color) { m_colors.insert(id, color); }
    void clear() { m_colors.clear(); }
    bool contains(const QString &id) const { return m_colors.contains(id); }
    int count() const { return m_colors.size(); }

    // 命中返回注入实色；未命中返回传入的历史兜底色（保证外观不变）。
    QColor color(const QString &id, const QColor &fallback) const
    {
        return m_colors.value(id, fallback);
    }

private:
    QHash<QString, QColor> m_colors;
};

} // namespace nextpilot::plotscope::fastplot
