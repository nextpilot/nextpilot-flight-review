#include "ProtocolModule.h"

#include "BuiltinModuleRegistry.h"
#include "IUiContribution.h"
#include "IModule.h"

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

ModuleInfo ProtocolModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.protocol"),
        QStringLiteral("协议模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("protocol.editor")}, // 提供协议编辑/校验能力
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

void ProtocolModule::initialize(ModuleContext &)
{
    // 纯逻辑在 ng/libs/protocol（ng_protocol）+ SPI v2 CustomProtocolParser；
    // 本模块只负责 UI 入口，核心能力已由库与自定义解析器提供，无需额外 handler。
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
