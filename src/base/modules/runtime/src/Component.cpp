#include "Component.h"

#include "ps_extension_api.h"

namespace ps::components
{

void Registry::addProtocol(const ProtocolContribution &protocol)
{
    m_protocols.push_back(protocol);
}

void Registry::addRibbon(const RibbonContribution &ribbon)
{
    m_ribbon.push_back(ribbon);
}

void Registry::addAction(const QString &id, std::function<void(const ActionContext &)> handler)
{
    m_actions.push_back({id, std::move(handler)});
}

void Registry::addDock(const DockContribution &dock)
{
    m_docks.push_back(dock);
}

void Registry::addDialog(const DialogContribution &dialog)
{
    for (const DialogContribution &existing : m_dialogs)
        if (existing.id == dialog.id)
            return;
    m_dialogs.push_back(dialog);
}

void Registry::addScriptRunner(const ScriptRunnerContribution &runner)
{
    if (runner.run)
        m_scriptRunner = runner;
}

void Registry::addDataSource(const DataSourceSpec &source)
{
    if (!source.descriptor)
        return;
    for (const DataSourceSpec &existing : m_dataSources)
        if (existing.descriptor == source.descriptor)
            return;
    m_dataSources.push_back(source);
}

const ProtocolContribution *Registry::protocol(const QString &kind) const
{
    for (const ProtocolContribution &p : m_protocols)
        if (p.kind == kind)
            return &p;
    return nullptr;
}

QDialog *Registry::createDialog(const QString &id, const QVariantMap &args, QWidget *parent) const
{
    for (const DialogContribution &dialog : m_dialogs)
        if (dialog.id == id && dialog.create)
            return dialog.create(args, parent);
    return nullptr;
}

ScriptResult Registry::runScript(const QString &source, const QVector<ScriptSeries> &inputs,
                                 const QMap<QString, double> &params, const QString &language) const
{
    if (!m_scriptRunner.run)
    {
        ScriptResult result;
        result.error = QStringLiteral("未安装脚本运行组件");
        return result;
    }
    return m_scriptRunner.run(source, inputs, params, language);
}

bool Registry::invoke(const QString &action, const ActionContext &ctx) const
{
    for (const auto &entry : m_actions)
    {
        if (entry.first == action)
        {
            entry.second(ctx);
            return true;
        }
    }
    return false;
}

ExtensionRegistry &ExtensionRegistry::instance()
{
    static ExtensionRegistry registry;
    return registry;
}

bool ExtensionRegistry::addExtension(const ExtensionDescriptor &descriptor, ExtensionFactory factory)
{
    if (descriptor.id.isEmpty() || !factory)
        return false;
    for (const ExtensionEntry &entry : m_extensions)
        if (entry.descriptor.id == descriptor.id)
            return false;
    m_extensions.push_back({descriptor, std::move(factory)});
    return true;
}

} // namespace ps::components
