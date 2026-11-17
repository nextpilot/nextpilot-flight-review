#include "DataModule.h"

#include "DataRepository.h"
#include "Events.h"

#include <cstdio>

using namespace ng::core;
using namespace ng::host;

ModuleInfo DataModule::info() const
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

void DataModule::initialize(ModuleContext &ctx)
{
    // 只订阅仓库事件；不碰 UI、不认识格式。用 this 作 context object，
    // 模块析构时连接自动断开——不需要手动解绑。
    connect(&ctx.data(), &DataRepository::datasetAdded, this, [](DatasetId id) {
        fprintf(stdout, "[data] 数据集已加入: #%d\n", id);
    });
    connect(&ctx.data(), &DataRepository::datasetRemoved, this, [](DatasetId id) {
        fprintf(stdout, "[data] 数据集已移除: #%d\n", id);
    });
}

#include "DataModule.moc"
