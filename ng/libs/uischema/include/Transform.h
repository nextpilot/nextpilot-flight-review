#pragma once

#include "ParamSpec.h"
#include "Result.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

namespace nextpilot::plotscope::uischema
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// 变换的统一契约：能力位 + 自描述。
//
// 用户原话："算子和 lua 脚本要明确自己的能力范围和自描述信息，方便用户选择，
// 否则选择错了，就死了。" 于是每一个可选的变换（内置算子 / 数学表达式 /
// Lua 算子 / Lua 脚本）都必须是一份 TransformDescriptor：
//   · capability 位标志声明它能用在哪些场景；
//   · 选择器按能力过滤，能力不匹配的根本不显示（选错在 UI 层就被挡住）；
//   · description / example / paramSchema 让人看得懂怎么填。
//
// 执行位置（用户 3 次澄清后的定稿）：
//   · Builtin / Expression -> Rust kernel（数据源衍生、提取前预处理都在 Rust）；
//   · Lua                  -> C++ TransformEvaluator（复用 LuaEngine，便于在线调试）。
// 二者对上层统一经 TransformEvaluator 调度，调用方不关心在哪执行。

enum class TransformKind : uint8_t
{
    Builtin,    // Rust 内置算子（ps-ops/filters），参数 JSON
    Expression, // 数学表达式（如 5*x+10/sin(x)），Rust 求值
    Lua,        // Lua 代码 / 脚本引用（可内嵌代码或按 scriptId 引用），C++ 求值
};

// 能力位：一个变换可同时具备多种用途。
enum TransformCapability : uint32_t
{
    CapNone      = 0,
    CapDerived   = 1u << 0, // 数据源衍生列 / channel 变换
    CapAxisX     = 1u << 1, // 曲线 X 轴变换（主要是时间 offset）
    CapAxisY     = 1u << 2, // 曲线 Y 轴变换
    CapChecksum  = 1u << 3, // 协议 checksum
    CapTransform = CapDerived | CapAxisX | CapAxisY | CapChecksum,
};

QString transformKindName(TransformKind kind);
TransformKind transformKindFromName(const QString &name);
QString capabilityNames(uint32_t caps); // "衍生/轴X/轴Y/Checksum"
bool capabilitiesContain(uint32_t caps, TransformCapability cap);

// 一个参数说明（给 UI 生成编辑框 / tooltip）。
struct TransformParam
{
    QString key;
    QString label;
    QString type;        // number / string / bool
    QString description;
    QString defaultValue;
};

// 一份自描述变换定义（注册进 TransformRegistry）。
struct TransformDescriptor
{
    QString id;          // 稳定 id："builtin.scale" / "expression" / "lua.<scriptId>"
    QString label;       // 展示名："缩放/偏移" / "数学表达式" / "我的校验脚本"
    TransformKind kind = TransformKind::Builtin;
    uint32_t capabilities = CapNone;
    QString description; // 人话说明：它做什么、适用什么
    QString example;     // 示例（表达式文本 / Lua 片段 / 参数 JSON）
    QList<TransformParam> params;
    bool builtin = false; // 内置定义不可被用户覆盖

    bool supports(TransformCapability cap) const { return capabilitiesContain(capabilities, cap); }
    QJsonObject toJson() const;
    static TransformDescriptor fromJson(const QJsonObject &obj);
};

// 把变换参数适配为纯 metadata 规格，供 SchemaEngine 统一编译表单。
QVector<nextpilot::plotscope::uischema::ParamSpec>
transformParamSpecs(const TransformDescriptor &descriptor);

// 变换求值请求：descriptor id + 一种载荷 + 参数。
// 三场景共用；执行由 TransformEvaluator 按 descriptor.kind 路由。
struct TransformRequest
{
    QString descriptorId;
    TransformKind kind = TransformKind::Builtin;
    QString expression;        // kind==Expression 的表达式文本
    QString luaSource;         // kind==Lua 的代码（内嵌）
    QString scriptId;          // kind==Lua 且用脚本库引用时
    QJsonObject params;        // 参数（builtin 算子参数 / Lua param 等）

    QJsonObject toJson() const;
    static TransformRequest fromJson(const QJsonObject &obj);
};

// 脚本库能力：GUI 不依赖 script 模块，由 script 模块经 AppContext 注入这组回调。
// 只用 Qt/core 类型，保持无 script 依赖、headless 可测。
struct TransformScriptOps
{
    using ResolveFn = std::function<QByteArray(const QString &scriptId, const QString &snapshot)>;
    using SaveFn = std::function<Result<TransformDescriptor>(const QString &name, const QByteArray &source)>;
    using DescribeFn = std::function<Result<TransformDescriptor>(const QByteArray &source, const QString &name)>;

    ResolveFn resolve;  // 文件源码优先、快照兜底
    SaveFn save;        // 写 scripts/ops/<name>.lua 并注册
    DescribeFn describe; // 只解析 describe()，不落盘

    bool valid() const { return static_cast<bool>(resolve) && static_cast<bool>(save) && static_cast<bool>(describe); }
};

// Transform 注册表：纯数据，headless 可见。内置项在 core 静态构造时注册；
// 用户的 Lua 脚本项由 script 模块加载脚本库后注册。
class TransformRegistry
{
public:
    static void registerDescriptor(const TransformDescriptor &descriptor);
    static bool contains(const QString &id);
    static TransformDescriptor get(const QString &id);
    static QList<TransformDescriptor> all();
    // 按能力过滤：UI 选择器只列这些——这是"防止选错"的核心接口。
    static QList<TransformDescriptor> byCapability(TransformCapability cap);
    static QStringList idsByCapability(TransformCapability cap);
    static void clear();
};

} // namespace nextpilot::plotscope::uischema
