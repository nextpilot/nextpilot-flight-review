#pragma once

// A8b-M2 裁定 52441074 §3：语义色 token 读回经服务接口，壳不直连 registry Hub。
// 纯数据 ThemeTokenSpec；实现放 assembly，内部从唯一扩展注册表汇总。
// test_gui 的 moduleThemeTokens 断言改为精确集合（plot.cursor/data.source/script.breakpoint）。

#include "Contribution.h" // ThemeTokenSpec（纯数据）

#include <QList>

namespace nextpilot::plotscope::workbench
{

class IThemeService
{
public:
    virtual ~IThemeService() = default;

    // 扩展 token 全集：包 token 优先、模块 token 兜底，按 id 去重（先到者优先）、
    // 按 id 升序。壳（MainWindow::refreshThemeTokens）一次取齐，不再自己合并两路来源。
    virtual QList<nextpilot::plotscope::registry::ThemeTokenSpec> tokens() const = 0;
};

} // namespace nextpilot::plotscope::workbench
