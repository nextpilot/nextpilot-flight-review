#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// 数据贡献里描述的一个数据资产（样例数据 / 预置数据集 / 测试夹具）。
struct DataAsset
{
    QString id;          // 稳定标识
    QString label;       // 显示名
    QString location;    // 资源路径（相对知识包根目录 / qrc / 绝对，由实现约定）
    QString format;      // 建议的格式 kind（如 "csv"/"ulog"），供加载器参考，可空
    QString description;
};

// 数据贡献：一个知识包随附的数据资产集合。
// 它不是"格式驱动"——格式驱动是"如何读"，数据贡献是"读什么"（现成的数据样本）。
class IDataContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Data; }
    virtual QList<DataAsset> assets() const = 0;
};

} // namespace nextpilot::plotscope::registry
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using registry::DataAsset;
using registry::IDataContribution;
} // namespace nextpilot::plotscope::core

