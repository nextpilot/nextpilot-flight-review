#pragma once

#include "FilterEngine.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;

// 内嵌的 12 个 PJ4 内置过滤器的 Luau 源（CMake 从 resources/builtin_filters.luau 生成）。
// 纯 C++ 返回，无 Qt resource 依赖，headless / CLI 均可读。
QByteArray bundledBuiltinFiltersSource();

// 一条已编目过滤器类 + 来源。
struct CatalogueEntry
{
    FilterDescriptor cls;
    QString origin;
    bool bundled = true;
};

// Qt-free 的自描述过滤器类注册表。
// 唯一来源是 bundled 多类资源文本；不扫目录。duplicate id 首个注册胜出。
class FilterCatalogue
{
public:
    explicit FilterCatalogue(std::shared_ptr<FilterEngine> engine);

    // 编译 source 并注册它声明的全部 class；返回新增条数。
    Result<int> addBundledSource(const QByteArray &source, const QString &origin);

    const QVector<CatalogueEntry> &entries() const { return m_entries; }
    const CatalogueEntry *find(const QString &id) const;

    // 由已编目 id + 参数建一个可用处理器（有状态实例）。
    Result<std::unique_ptr<FilterInstance>> makeProcessor(const QString &id, const QJsonObject &params) const;

    // 由布局内嵌的模块源 + id 建处理器（id 不在编目时的恢复路径）。
    Result<std::unique_ptr<FilterInstance>> makeProcessorFromSource(const QByteArray &source, const QString &id,
                                                                    const QJsonObject &params) const;

    // 只返回 class id 的自包含 Luau 模块；create 会把未提供参数填成 descriptor default。
    std::optional<QByteArray> standaloneClassScript(const QString &id) const;

    // 把 descriptor 编成 GUI 可直接生成表单的 JSON（字段与 ParamSpec 对齐）；不存在返回 null JSON。
    static QJsonObject descriptorToJson(const FilterDescriptor &d);

    // 对一条序列逐样本应用过滤器，返回 {time:[f64...], value:[f64...]}。
    // t/v 为绝对时间/值；按首样本重基为相对秒喂给实例，处理 suppress/显式时间后加回基准。
    Result<QJsonObject> applySeries(const QString &id, const QJsonObject &params,
                                    const QVector<double> &t, const QVector<double> &v) const;

private:
    std::shared_ptr<FilterEngine> m_engine;
    QVector<CatalogueEntry> m_entries;
};

// 便捷工厂：默认 Lua 引擎 + 内嵌 bundled 源，构造即加载完毕。
std::shared_ptr<FilterCatalogue> makeBundledFilterCatalogue();

} // namespace nextpilot::plotscope::script
