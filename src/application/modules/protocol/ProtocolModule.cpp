#include "ProtocolModule.h"

#include "ProtocolIntentHandler.h"
#include "BuiltinModuleHub.h"
#include "Contribution.h"
#include "IWorkbenchContribution.h"
#include "InstantiationService.h"
#include "ICommands.h"
#include "CommandAdapter.h"
#include "RuntimeHost.h"

using nextpilot::plotscope::instantiation::InstantiationService;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

ModuleInfo ProtocolModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.protocol"),
        QStringLiteral("协议模块"),
        QStringLiteral("1.0"),
        // 能力声明与 ProtocolIntentHandler 的 op 表一一对应：
        // 声明了却不实现，等于对外承诺一个空入口。
        {QStringLiteral("protocol.editor"), QStringLiteral("protocol.validate"),
         QStringLiteral("protocol.preview"), QStringLiteral("protocol.templates")},
        {}                                  // 不依赖其它模块
    };
}

IWorkbenchContribution *ProtocolModule::create() { return new ProtocolModule(); }

ModuleInfo ProtocolModule::info() const
{
    return moduleInfo();
}

void ProtocolModule::initialize(InstantiationService &services)
{
    // 纯逻辑在协议库（ps_protocol）+ SPI v2 CustomProtocolParser；
    // 模块负责把"模板校验/单帧试算/模板库"这三件界面之外也成立的事做成意图，
    // 让脚本与 CLI 不必复制一份校验逻辑，也不必经过任何对话框。
    auto *cmds = services.getService<nextpilot::plotscope::workbench::ICommands>(QStringLiteral("vscode.commands"));
    if (!cmds)
        return;
    m_handler = new ProtocolIntentHandler(this);
    m_cmds = cmds;
    auto reg = [this, cmds](const QString &id, nextpilot::plotscope::intent::IIntentHandler *h) {
        m_commandIds.append(id);
        cmds->registerCommand(id, nextpilot::plotscope::workbench::asCommandHandler(h, id));
    };
    for (const QString &op : ProtocolIntentHandler::supportedOps())
        reg(op, m_handler);
}

ModuleContributions ProtocolModule::contributes() const
{
    ModuleContributions c;
    c.owner = QStringLiteral("protocol");
    c.ribbonPages.append(RibbonPageDecl{QStringLiteral("ribbon.page.protocol"),
                                        QStringLiteral("协议"), 5});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.protocol.editor"),
                                          QStringLiteral("编辑器"),
                                          QStringLiteral("ribbon.page.protocol"), 0});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.protocol.debug"),
                                          QStringLiteral("检查"),
                                          QStringLiteral("ribbon.page.protocol"), 1});

    // 协议页"编辑器"组：新建 / 打开 / 校验 + 编辑器入口；外壳按 id 特判弹窗。
    auto add = [&c](const QString &id, const QString &label, const QString &area,
                    const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = area;
        a.icon = icon;
        a.order = order;
        c.actions.append(a);
    };
    const QString editor = QStringLiteral("ribbon.group.protocol.editor");
    add(QStringLiteral("protocol.new"), QStringLiteral("新建协议"), editor, QStringLiteral("plus"), 0);
    add(QStringLiteral("protocol.open"), QStringLiteral("打开编辑"), editor, QStringLiteral("file_open"), 1);
    add(QStringLiteral("protocol.validate"), QStringLiteral("校验协议"), editor, QStringLiteral("check"), 2);
    add(QStringLiteral("protocol.editor"), QStringLiteral("协议编辑器"), editor, QStringLiteral("settings"), 3);
    add(QStringLiteral("app.action.protocolEditor"), QStringLiteral("协议编辑器"),
        editor, QStringLiteral("file_open"), 4);
    const QString debug = QStringLiteral("ribbon.group.protocol.debug");
    add(QStringLiteral("app.action.protocolInspector"), QStringLiteral("协议检查器"),
        debug, QStringLiteral("file_open"), 0);
    return c;
}

void ProtocolModule::onAttach(IUiContext &ui)
{
    Q_UNUSED(ui);
    // A8b-M2：UI 声明已迁至 contributes()，宿主统一灌回。
}

void ProtocolModule::onDetach()
{
    if (m_cmds)
        for (const QString &id : m_commandIds)
            m_cmds->unregisterCommand(id);
    m_commandIds.clear();
}

#include "ProtocolModule.moc"

PS_REGISTER_MODULE(ProtocolModule)
