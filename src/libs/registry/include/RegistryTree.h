#pragma once

// M2.8 Registry 机制整改（R1）：全系统注册树（等价 rt_object kernel object container）。
//
// 组合根（AppContext）创建持有——不做静态单例，测试间树完全隔离。
// 树只存节点的**弱引用**（QPointer）：static 节点（M2.8 R4 的 BuiltinModule/
// BuiltinModule）生命周期不归树管，进程退出时对象自灭、树侧指针自动变空。

#include "RegistryNode.h"

#include <QList>
#include <QPointer>

namespace nextpilot::plotscope::registry
{

class RegistryTree
{
public:
    // 节点构造时自动调（RegistryNode 构造）。同一节点重复 attach 是空操作。
    void attach(RegistryNode *node);
    // 节点析构时自动调（RegistryNode 析构）。
    void detach(RegistryNode *node);

    // 全树枚举（按上树顺序；已亡节点自动跳过并清理）。
    QList<RegistryNode *> nodes() const;
    // 按 kind 找节点（首个匹配；kind 是身份，同一树上常规不重kind）。
    RegistryNode *find(RegistryKind kind) const;

    ~RegistryTree();

private:
    QList<QPointer<RegistryNode>> m_nodes;
};

} // namespace nextpilot::plotscope::registry
