#pragma once

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::host
{

// 模块的静态描述，宿主据此做依赖解析与加载决策。
// provides/requires 是"能力"名，不是具体模块 id——解耦具体实现。
//
// 【术语定标】模块（Module）= app 基底功能，无业务知识，静态自注册、
// 编译期固定（只能编译时增删）；没有运行时"动态模块"概念，故无 kind 字段。
// 业务知识由扩展承载：工具箱（Toolbox，内部扩展，静态自注册 + 运行时
// mount/unmount）与插件（Plugin，外部扩展，C ABI 扫描注册）。
struct ModuleInfo
{
    QString id;
    QString name;
    QString version;
    QStringList provides;
    QStringList requires;
};

} // namespace nextpilot::plotscope::host
