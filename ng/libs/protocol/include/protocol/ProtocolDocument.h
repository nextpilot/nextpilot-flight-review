#pragma once

#include "protocol/TypedVec.h"

#include <QJsonObject>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nextpilot::plotscope::protocol
{

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

// 表达式节点。字面量两种：整型（模板里的 count/expect/掩码）与浮点。
// 分开存是为了让 / 与 % 能按操作数类型决定整除还是浮点除——double 存不下 u64。
struct Expr
{
    enum Kind { Num, Ref, Bin, Un, Fn } kind = Num;
    double num = 0.0;    // Num 且 !isInt 时有效
    int64_t ival = 0;    // Num 且 isInt 时有效
    bool isInt = true;
    std::string name;
    std::string fn;
    int op = 0;
    ExprPtr a, b, c;
};

// 一个段的值域描述。类型即 ValueType（与 TypedVec 同一套枚举），size 是单元素字节宽。
struct TypeInfo
{
    ValueType type = ValueType::U8;
    uint32_t size = 1;
};

// 段的输出形态，取代旧的 hidden/plot 两个布尔。
enum class EmitMode
{
    Channel, // 出通道（默认）
    Aux,     // 参与表达式/校验，但不出通道
    None     // 只占位/校验，不出通道
};

struct CheckSpec
{
    std::string op = "eq";
    ExprPtr rhs;
    ExprPtr rhs2;
    ExprPtr rhs3;
    std::vector<TypedValue> values;
    std::vector<std::string> over;
    std::string scope = "element";
    uint32_t poly = 0;
    uint32_t init = 0;
    uint32_t xorout = 0;
    bool refin = false;
    bool refout = false;
    int width = 0;
};

// 长度段反算：本段声明值必须等于后面字段（或表达式）合计。
struct LengthSpec
{
    bool enabled = false;
    std::vector<std::string> fields;
    std::string unit = "elements";
    ExprPtr expr;
};

struct SegmentDef
{
    std::string name;
    std::string unit;
    std::string description;
    std::string prefix; // 变体段的通道名前缀
    TypeInfo type;
    ExprPtr countExpr; // 元素个数（字节宽由 type 推算）
    double scale = 1.0;
    double offset = 0.0;
    bool bigEndian = false;
    EmitMode emitMode = EmitMode::Channel;
    ExprPtr expectExpr; // 非空即常量匹配
    std::vector<CheckSpec> checks;
    LengthSpec length;
};

struct VariantDef
{
    std::vector<std::string> keys;
    std::string name;
    std::vector<SegmentDef> segments;
};

struct SeriesDef
{
    bool enabled = false;
    ExprPtr selector;
    std::string payload;
    std::string fallback = "reject";
    std::vector<VariantDef> variants;
};

using SegSpec = SegmentDef;
using VariantSpec = VariantDef;
using SeriesSpec = SeriesDef;

/// 一份协议的唯一类型化表示：JSON 由 codec 解析进来，编辑器和校验器共用同一份结构。
struct ProtocolDocument
{
    std::string version = "1";
    std::string name = "frame";
    std::string description;
    std::string layout = "sequential";
    std::string endian = "little";
    uint32_t maxsize = 4096;
    std::vector<SegmentDef> segments;
    SeriesDef series;
    bool hasTimestamp = false;
    ExprPtr timestampExpr;
    std::string timestampUnit = "s";
    std::string timestampOrigin = "first_frame";
    bool timestampRaw = false;
    std::string timestampFallback = "reject";
};

// 表达式求值上下文：每段一列值，列自带类型。
struct EvalCtx
{
    std::map<std::string, TypedVec> values;

    bool get(const std::string &n, int index, TypedValue &out, std::string &err) const;
};

/// 类型化求值。整型/浮点不隐式混算；越界与除零显式报错。
bool evalExpr(const ExprPtr &e, const EvalCtx &ctx, TypedValue &out, std::string &err);

/// 便捷：只接受整型结果（count/selector/timestamp 等结构量走这个）。
bool evalExprInt(const ExprPtr &e, const EvalCtx &ctx, int64_t &out, std::string &err);
/// 便捷：接受任意数值结果并显式降级为 double（仅用于展示/校验比较）。
bool evalExprDouble(const ExprPtr &e, const EvalCtx &ctx, double &out, std::string &err);

/// 解析协议 JSON 为唯一类型化文档；失败返回 false 并写 err。
bool parseDocument(const QJsonObject &root, ProtocolDocument &doc, std::string &err);

/// 把类型化文档写回协议 JSON；用于编辑器和保存的唯一序列化入口。
QJsonObject toJson(const ProtocolDocument &doc);

/// 表达式文本化，供编辑器表格显示和 JSON 回写。
std::string exprText(const ExprPtr &e);

} // namespace nextpilot::plotscope::protocol
