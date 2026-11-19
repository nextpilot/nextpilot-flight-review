#include "DataModule.h"

#include "BuiltinModuleRegistry.h"
#include "DataRepository.h"
#include "Events.h"
#include "IUiContribution.h"
#include "IModule.h"

#include <cstdio>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

ModuleInfo DataModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.data"),
        QStringLiteral("数据模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("data.session")},
        {}
    };
}

IModule *DataModule::create()
{
    return new DataModule();
}

ModuleInfo DataModule::info() const
{
    return moduleInfo();
}

void DataModule::initialize(ModuleContext &ctx)
{
    // 只订阅仓库事件；不碰 UI、不认识格式。用 this 作 context object，
    // 模块析构时连接自动断开——不需要手动解绑。
    connect(&ctx.app().data(), &DataRepository::datasetAdded, this, [](DatasetId id) {
        fprintf(stdout, "[data] 数据集已加入: #%d\n", id);
    });
    connect(&ctx.app().data(), &DataRepository::datasetRemoved, this, [](DatasetId id) {
        fprintf(stdout, "[data] 数据集已移除: #%d\n", id);
    });
}

void DataModule::onAttach(UiContext &ui)
{
    // GUI 环境下才走到这里（headless 时宿主不调用 onAttach）。
    //
    // 模块只声明"我需要一个数据集树面板"，由 app 骨架的部件工厂决定用哪个实现——
    // 因此本模块不依赖 Qt Widgets，CLI（headless）构建保持纯净。
    //
    // 若将来新增另一个模块想自带界面，只需在这里 requestPanel + declareSlot，
    // 无需改动数据模块、GUI 层或任何其他文件。
    ui.requestPanel(QStringLiteral("dock.left"), QStringLiteral("dataset-tree"));

    // 数据功能 ribbon 的"常用"动作（对应老代码 DataSourceRibbon 的常用组）。
    // 每个都绑定**真实存在**的意图处理器（宿主级 data.* handler），不是空按钮。
    // 打开数据在骨架里已有 app.action.openData（MainWindow 处理文件对话框），
    // 这里补其余三个可用纯意图驱动的：重载 / 导出 / 清缓存。
    // order 让组内顺序可控：骨架的"快速打开/最近"占 0/1（见 MainWindow），
    // 这里从 10 起排 重载/导出/清缓存，抄老 DataSourceRibbon"常用"组的相对顺序。
    auto add = [&ui](const QString &id, const QString &label,
                     const QString &op, const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.area = QStringLiteral("ribbon.group.data.common");
        a.intentOp = op;
        a.icon = icon;
        a.order = order;
        ui.addAction(a);
    };
    add(QStringLiteral("data.action.reload"), QStringLiteral("重载"),
        QStringLiteral("data.reload"), QStringLiteral("reset"), 10);
    add(QStringLiteral("data.action.export"), QStringLiteral("导出 CSV"),
        QStringLiteral("data.export"), QStringLiteral("export_data"), 11);
    add(QStringLiteral("data.action.clearCache"), QStringLiteral("清解析缓存"),
        QStringLiteral("data.clearCache"), QStringLiteral("trash"), 12);
}

#include "DataModule.moc"

// 编译期自注册：宿主启动时遍历全局表装载，新增模块无需改 App / 宿主。
NG_REGISTER_MODULE(DataModule)
