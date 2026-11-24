#pragma once

// 语义色接缝：fastplot 不知道自己被谁用，也不认识主题 token 表。
//
// 画布上"游标 / A-B 测量 / 网格"这类颜色的语义由上层（plot 模块 + 主题）决定，
// 但 fastplot 只依赖 Qt Widgets（见本库 CMakeLists 首段约束），不能反向 include
// gui/theme。所以这里留一个**进程级只读表 + 写入口**：
//
//   * 上层（MainWindow::refreshThemeTokens）在主题生效/切换后调用
//     setSemanticColor(id, color)，把 ThemeTokenSpec 解析出的实色推下来；
//   * 库内绘制点调用 semanticColor(id, fallback)，取不到就用绘制点自己的兜底色。
//
// 这样"token 声明了却没人读"不可能再发生：读的一方是绘制路径本身，判据是像素。
//
// 线程模型：fastplot 只在 GUI 线程绘制（Qt Widgets 规定），故用普通 QHash，
// 不加锁；测试可在同线程调 clearSemanticColors() 复位。

#include <QColor>
#include <QString>

namespace nextpilot::plotscope::fastplot
{

// 写入一条语义色（id 形如 "plot.cursor"）。颜色非法则忽略，不覆盖已有值。
void setSemanticColor(const QString &id, const QColor &color);

// 取语义色；未推送时返回 fallback（调用点自带的兜底色）。
QColor semanticColor(const QString &id, const QColor &fallback);

// 是否已被上层显式推送（诊断/测试用：区分"用了兜底"与"主题真给了色"）。
bool hasSemanticColor(const QString &id);

// 清空整表（测试用）。
void clearSemanticColors();

} // namespace nextpilot::plotscope::fastplot
