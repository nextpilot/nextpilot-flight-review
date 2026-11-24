#pragma once

#include <QColor>
#include <QHash>
#include <QString>

namespace nextpilot::plotscope::uischema
{

// 主题 token 纯数据层：语义 token -> 双主题色值 + 系列分类色 + 可读性适配。
//
// 定位（契约 D6 / c6）：本表与纯取色函数不持有状态、不碰 QWidget / QApplication，
// 只依赖 QtGui 值类型（QColor；口径同 ps_command 用 QKeySequence——Gui 级，
// 不是 Widgets，headless 亦可链）。主题当前态、Settings 持久化、扩展包兜底色、
// QSS / palette 生成与 theme.* 意图认领留在 gui 层 ThemeManager。
class ThemeTokens
{
public:
    enum class Theme
    {
        Light,
        Dark
    };

    // 内置 token 色表：两套主题各一份，token 名一一对应。
    // 未知 token 返回醒目洋红 #FF00FF——缺失 token 一眼可见，不静默退化成黑/白。
    static QColor color(Theme theme, const QString &token);

    // 数据系列分类色板（集中定义，不散落业务代码）；按主题做可读性适配。
    static QColor seriesColor(int index, Theme theme);

    // 让同一语义色在两套背景上都可读：暗色向白提亮，亮色向黑压暗，保留色相。
    static QColor adaptColor(const QColor &c, Theme theme);

    // 整套色表（供上层把 QSS 片段里的 $(token) 占位符替换成实色）。
    static const QHash<QString, QColor> &table(Theme theme);
};

} // namespace nextpilot::plotscope::uischema
