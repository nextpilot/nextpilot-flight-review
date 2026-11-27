#pragma once

// M2.8 R4：static 注册表的**节点宿主**（host 层持有——libs 边界守卫禁止 Meyers
// 单例进 libs，见 LibBoundaryGuard.cmake；docs/roadmap/M2.8-Registry整改.md §2.6
// 拍板 4「static 注册表由 host 装配时显式构造上树」）。
//
// 各节点的数据全部读各自注册表的纯值存储（M2.8 遗留 4 收口：三张存量静态表
// 全量上树，diag.registries 无盲区）——QObject 实例只在 host 装配期（main 内）
// 构造，静态初始化期不触达。生命周期归 RuntimeHost（QObject 父子树），树侧只存
// QPointer 弱引用，析构自动摘除——树不托管 static 节点生命周期（红线 4）。
//
// count/keys 每次调用实时读表：静态表装满后只读，ParserHub 将来接 M3 插件
// 运行期增删时节点视图自动跟随，无需信号（diag 面板刷新即见最新）。

#include "BuiltinModuleHub.h"
#include "ParserHub.h"
#include "RecordingBackend.h"
#include "RecordingFormat.h"
#include "RegistryNode.h"
#include "RegistryTree.h"

#include <QStringList>

namespace nextpilot::plotscope::host {

// 内置模块节点：模块清单经节点间接可枚举（diag.registries 用）。
class BuiltinModuleNode : public nextpilot::plotscope::registry::RegistryNode
{
public:
    explicit BuiltinModuleNode(nextpilot::plotscope::registry::RegistryTree &tree, QObject *parent = nullptr)
        : RegistryNode(tree, nextpilot::plotscope::registry::RegistryKind::BuiltinModule, QStringLiteral("内置模块"),
                       parent)
    {
    }

    int count() const override;
    QStringList keysAsStrings() const override;
};

// 解析器节点：解析器清单经节点间接可枚举（diag.registries 用）。
// 混合表视图——builtin 静态自注册 + M3 插件运行期增删，实时读表。
class ParserNode : public nextpilot::plotscope::registry::RegistryNode
{
public:
    explicit ParserNode(nextpilot::plotscope::registry::RegistryTree &tree, QObject *parent = nullptr)
        : RegistryNode(tree, nextpilot::plotscope::registry::RegistryKind::Parser, QStringLiteral("解析器"), parent)
    {
    }

    int count() const override { return nextpilot::plotscope::parser::ParserHub::count(); }
    QStringList keysAsStrings() const override { return nextpilot::plotscope::parser::ParserHub::ids(); }
};

// 录制格式节点：格式清单经节点间接可枚举（diag.registries 用）。
class RecordingFormatNode : public nextpilot::plotscope::registry::RegistryNode
{
public:
    explicit RecordingFormatNode(nextpilot::plotscope::registry::RegistryTree &tree, QObject *parent = nullptr)
        : RegistryNode(tree, nextpilot::plotscope::registry::RegistryKind::RecordingFormat, QStringLiteral("录制格式"),
                       parent)
    {
    }

    int count() const override { return nextpilot::plotscope::rec::RecordingFormatHub::count(); }
    QStringList keysAsStrings() const override { return nextpilot::plotscope::rec::RecordingFormatHub::keys(); }
};

// 录制后端节点：后端清单经节点间接可枚举（diag.registries 用）。
class RecordingBackendNode : public nextpilot::plotscope::registry::RegistryNode
{
public:
    explicit RecordingBackendNode(nextpilot::plotscope::registry::RegistryTree &tree, QObject *parent = nullptr)
        : RegistryNode(tree, nextpilot::plotscope::registry::RegistryKind::RecordingBackend, QStringLiteral("录制后端"),
                       parent)
    {
    }

    int count() const override { return nextpilot::plotscope::rec::RecordingBackendHub::count(); }
    QStringList keysAsStrings() const override { return nextpilot::plotscope::rec::RecordingBackendHub::keys(); }
};

} // namespace nextpilot::plotscope::host
