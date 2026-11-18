#include "DataModule.h"

#include "BuiltinModuleRegistry.h"
#include "DataRepository.h"
#include "Events.h"
#include "IModule.h"

#include <cstdio>

using namespace ng::core;
using namespace ng::host;

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
}

#include "DataModule.moc"

// 编译期自注册：宿主启动时遍历全局表装载，新增模块无需改 App / 宿主。
NG_REGISTER_MODULE(DataModule)
