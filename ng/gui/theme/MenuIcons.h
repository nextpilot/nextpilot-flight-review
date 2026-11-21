#pragma once

#include <QIcon>
#include <QString>

namespace nextpilot::plotscope::gui
{

// 右键菜单/动作的小图标（16pt 线性自绘，颜色一律走主题 token）。
//
// 为什么自绘而不是塞一套 PNG/SVG：本工程对"UI 颜色唯一来源是 ThemeManager"
// 有硬约束（AGENTS §12.1），深浅主题切换时位图资源会突然不可读；这里全部
// 用 QPainter + token 现画，切主题重取一次即可。也避免为了几个菜单项引入
// QtSvg 依赖。
//
// name 取值见 MenuIcons.cpp 的 paint()；未知 name 返回空 QIcon（调用方
// 直接 setIcon 空图标也不会崩，只是不显示——便于渐变式接入）。
class MenuIcons
{
public:
    static QIcon icon(const QString &name);
};

} // namespace nextpilot::plotscope::gui
