#pragma once

#include "Result.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

struct lua_State;

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;

// 过滤器参数控件类型。schema 是数据，GUI 据此生成表单。
enum class FilterParamType
{
    Number,
    Integer,
    Boolean,
    Enum,
    String,
    Text,
};

// 一个枚举选项：持久化值 + UI 标签。值用 QJsonValue 保型（int 枚举不退化成字符串）。
struct FilterEnumValue
{
    QJsonValue value;
    QString label;
};

// 一个参数描述（从 class.parameters 读取，不实例化过滤器）。
struct FilterParamSpec
{
    QString name;                    // params 表键 + JSON 键
    FilterParamType type = FilterParamType::Number;
    QString label;
    QString tooltip;
    QJsonValue defaultValue;
    bool hasMin = false;
    double min = 0.0;
    bool hasMax = false;
    double max = 0.0;
    bool hasStep = false;
    double step = 0.0;
    bool hasDecimals = false;
    int decimals = 0;
    QString unit;
    QVector<FilterEnumValue> values; // Enum only
    QString visibleWhenParam;        // 空=无条件
    QJsonValue visibleWhenEquals;
};

// 过滤器类描述（从 Lua 模块读取，不调 create）。source 是完整模块文本。
struct FilterDescriptor
{
    QString id;                      // 稳定 key（如 "derivative"）
    QString name;
    QString description;
    QString version;
    QString outputKind = QStringLiteral("double"); // double / int64 / same
    QVector<FilterParamSpec> parameters;
    QString source;                  // 完整模块文本，供实例构建与布局嵌入
    QString origin;                  // 来源标签（如 "bundled"）
};

// 一次 per-sample 计算结果。
struct FilterSampleResult
{
    bool suppress = false;           // true = 不产出（如微分首个样本）
    double value = 0.0;
    bool hasExplicitTime = false;    // true = 脚本返回了 (t, v)
    double outTime = 0.0;
};

// 有状态过滤器实例：一个脚本 VM 一个实例，两个过滤器不共享状态。
// 非线程安全，在宿主的指令预算保护调用内使用。
class FilterInstance
{
public:
    virtual ~FilterInstance() = default;

    // t = 自会话首样本起的秒数；v = 样本值。脚本错误经 failed()/error() 报告。
    virtual FilterSampleResult calculate(double t, double v) = 0;
    virtual void reset() = 0;
    virtual bool failed() const = 0;
    virtual QString error() const = 0;
};

// 过滤器脚本引擎（语言 seam）。当前实现基于 Lua 5.4；未来可加 Python 实现同一接口。
class FilterEngine
{
public:
    virtual ~FilterEngine() = default;

    // 编译 source 并读取它声明的全部 FilterClass（不调 create/calculate）。
    Result<QVector<FilterDescriptor>> inspectModule(const QByteArray &source, const QString &origin) const;

    // 由 descriptor + params JSON 建一个活实例（独立 VM，调用 create(params)）。
    Result<std::unique_ptr<FilterInstance>> createInstance(const FilterDescriptor &klass,
                                                            const QJsonObject &params) const;

    // 单类模块抽取统一走 FilterCatalogue::standaloneClassScript（PJ4 语义，含默认值注入与 id 注入防护）。

    // 最大 VM 指令数（看门狗），防止死循环；0 表示不限。
    void setInstructionBudget(int budget) { m_instructionBudget = budget; }
    int instructionBudget() const { return m_instructionBudget; }
    // 单 VM 内存上限（KB），0 表示不限。
    void setMemoryBudgetKb(int kb) { m_memoryBudgetKb = kb; }
    int memoryBudgetKb() const { return m_memoryBudgetKb; }

private:
    int m_instructionBudget = 200000; // 默认 20 万指令，足够滑窗/积分，挡住跑飞
    int m_memoryBudgetKb = 16384;     // 默认 16MB
};

// 默认引擎工厂（Lua 5.4 后端）。
std::shared_ptr<FilterEngine> makeLuaFilterEngine();

} // namespace nextpilot::plotscope::script
