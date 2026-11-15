#pragma once

class QApplication;

// 全局主题：一套 token + 一段 QSS + 深色 palette。
// 所有窗口（主窗、CSV 对话框、消息框）都只认这一份，避免逐窗口写 QSS 造成风格漂移。
// 颜色取自 design/prototype/PlotScope-工作区原型.html 的变量表。
namespace theme {

extern const char *kStyle;

void applyTo(QApplication &app);

} // namespace theme
