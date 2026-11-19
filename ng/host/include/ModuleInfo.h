#pragma once

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::host
{

enum class ModuleKind
{
    Builtin,    // 编译进 app，默认启用，可配置关闭
    Extension   // 独立动态库，运行时加载
};

// 模块的静态描述，宿主据此做依赖解析与加载决策。
// provides/requires 是"能力"名，不是具体模块 id——解耦具体实现。
struct ModuleInfo
{
    QString id;
    QString name;
    QString version;
    ModuleKind kind = ModuleKind::Builtin;
    QStringList provides;
    QStringList requires;
};

} // namespace nextpilot::plotscope::host
