#include "ProtocolModule.h"

#include "ProtocolIntentHandler.h"
#include "BuiltinModuleRegistry.h"
#include "IUiContribution.h"
#include "IModule.h"

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
        ModuleKind::Builtin,
        // 能力声明与 ProtocolIntentHandler 的 op 表一一对应：
        // 声明了却不实现，等于对外承诺一个空入口。
        {QStringLiteral("protocol.editor"), QStringLiteral("protocol.validate"),
         QStringLiteral("protocol.preview"), QStringLiteral("protocol.templates")},
        {}                                  // 不依赖其它模块
    };
}

IModule *ProtocolModule::create()
{
    return new ProtocolModule();
}

ModuleInfo ProtocolModule::info() const
{
    return moduleInfo();
}

void ProtocolModule::initialize(ModuleContext &ctx)
{
    // 纯逻辑在 ng/libs/protocol（ng_protocol）+ SPI v2 CustomProtocolParser；
    // 模块负责把"模板校验/单帧试算/模板库"这三件界面之外也成立的事做成意图，
    // 让脚本与 CLI 不必复制一份校验逻辑，也不必经过任何对话框。
    m_handler = new ProtocolIntentHandler(this);
    ctx.registerIntentHandler(m_handler);
}

void ProtocolModule::onAttach(UiContext &ui)
{
    // 协议页"编辑器"组：新建 / 打开 / 校验协议 + 编辑器入口。
    // 这些是 GUI 工具，按 action id 由外壳特判弹对话框（同 openData 的模式），
    // intentOp 留空但 id 语义明确，CLI/脚本不依赖它们。
    // order 抄老 ProtocolModule 编辑器面板 items 顺序：新建 / 打开 / 校验，
    // 编辑器入口排最后（老代码无此按钮，ng 补的整合入口）。
    auto add = [&ui](const QString &id, const QString &label, const QString &area,
                     const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.area = area;
        a.icon = icon;
        a.order = order;
        ui.addAction(a);
    };

    const QString editor = QStringLiteral("ribbon.group.protocol.editor");
    add(QStringLiteral("protocol.new"), QStringLiteral("新建协议"), editor, QStringLiteral("plus"), 0);
    add(QStringLiteral("protocol.open"), QStringLiteral("打开编辑"), editor, QStringLiteral("file_open"), 1);
    add(QStringLiteral("protocol.validate"), QStringLiteral("校验协议"), editor, QStringLiteral("check"), 2);
    add(QStringLiteral("protocol.editor"), QStringLiteral("协议编辑器"), editor, QStringLiteral("settings"), 3);
}

#include "ProtocolModule.moc"

NG_REGISTER_MODULE(ProtocolModule)
