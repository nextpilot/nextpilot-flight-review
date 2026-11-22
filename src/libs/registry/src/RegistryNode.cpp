#include "RegistryNode.h"
#include "RegistryTree.h"

namespace nextpilot::plotscope::registry
{

QString kindName(RegistryKind kind)
{
    switch (kind)
    {
    case RegistryKind::Dataset: return QStringLiteral("dataset");
    case RegistryKind::Source: return QStringLiteral("source");
    case RegistryKind::DerivedSeries: return QStringLiteral("derived-series");
    case RegistryKind::Interaction: return QStringLiteral("interaction");
    case RegistryKind::UiSlot: return QStringLiteral("ui-slot");
    case RegistryKind::Extension: return QStringLiteral("extension");
    case RegistryKind::PanelFactory: return QStringLiteral("panel-factory");
    case RegistryKind::BuiltinModule: return QStringLiteral("builtin-module");
    case RegistryKind::Parser: return QStringLiteral("parser");
    case RegistryKind::RecordingFormat: return QStringLiteral("recording-format");
    case RegistryKind::RecordingBackend: return QStringLiteral("recording-backend");
    }
    return QStringLiteral("unknown");
}

RegistryNode::RegistryNode(RegistryTree &tree, RegistryKind kind, const QString &name,
                           QObject *parent)
    : QObject(parent), m_kind(kind), m_name(name)
{
    // 构造即上树：继承即注册（rt_object 范式）。不存在不上树构造——
    // 注册表必须在树上，diag.registries 无盲区。
    tree.attach(this);
}

RegistryNode::~RegistryNode()
{
    // 析构自动摘除；树先亡时 m_tree 已被 RegistryTree 析构清空，跳过即可。
    if (m_tree)
        m_tree->detach(this);
}

} // namespace nextpilot::plotscope::registry
