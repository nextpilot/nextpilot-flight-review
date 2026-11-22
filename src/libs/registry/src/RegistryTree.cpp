#include "RegistryTree.h"

namespace nextpilot::plotscope::registry
{

RegistryTree::~RegistryTree()
{
    // 树先亡（组合根成员销毁顺序不保证）时通知节点摘掉反向指针，
    // 之后节点析构就不会再 touch 这棵已亡的树。
    for (const auto &ptr : m_nodes)
        if (ptr)
            ptr->m_tree = nullptr;
}

void RegistryTree::attach(RegistryNode *node)
{
    if (!node)
        return;
    for (const auto &ptr : m_nodes)
        if (ptr == node)
            return; // 重复 attach 空操作
    m_nodes.append(QPointer<RegistryNode>(node));
    node->m_tree = this;
}

void RegistryTree::detach(RegistryNode *node)
{
    if (!node)
        return;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        if (m_nodes.at(i) == node)
        {
            m_nodes.removeAt(i);
            break;
        }
    }
    if (node->m_tree == this)
        node->m_tree = nullptr;
}

QList<RegistryNode *> RegistryTree::nodes() const
{
    QList<RegistryNode *> out;
    // QPointer 弱引用：亡节点自动跳过（跳过的空位顺手从副本压实）。
    QList<QPointer<RegistryNode>> alive;
    for (const auto &ptr : m_nodes)
    {
        if (ptr)
        {
            out.append(ptr.data());
            alive.append(ptr);
        }
    }
    return out;
}

RegistryNode *RegistryTree::find(RegistryKind kind) const
{
    for (const auto &ptr : m_nodes)
        if (ptr && ptr->kind() == kind)
            return ptr.data();
    return nullptr;
}

} // namespace nextpilot::plotscope::registry
