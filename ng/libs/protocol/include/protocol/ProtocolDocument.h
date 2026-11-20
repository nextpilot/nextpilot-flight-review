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
    bool isTime = false; // type:"time"：text 文本时间 hhmmss[.ss]，解析成当天秒
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

// 位域窗口（SPEC §6）。位域段不推进字节游标，它是对某个字节区间的视图：
//   base 为空 → 相对帧起点；base 指向一个已解析的容器段（典型：CAN 的 8 字节 data）。
// order: lsb0 = DBC Intel（低位在前），msb0 = DBC Motorola（高位在前）。
struct BitSpec
{
    bool enabled = false;
    std::string base;
    int64_t offset = 0; // 单位是位，不是字节
    int width = 0;
    std::string order = "lsb0";
};

// text 数值变换（SPEC §7.4）。只处理"非标准记法"，标准 CSV/NMEA 数值不需要它。
struct TransformStep
{
    // sexagesimal：4807.038 → 48 + 7.038/60（split 是分的小数位数）
    // scale/mul（乘 k）、offset/add（加 b）、abs、round、clamp（min..max）
    std::string op;
    int split = 2;
    double k = 1.0;
    double b = 0.0;
    bool hasMin = false;
    bool hasMax = false;
    double minVal = 0.0;
    double maxVal = 0.0;
};

// 摘要校验（SPEC §10）：算什么（内置算子或 Lua 脚本）+ 算哪段原始字节。
// 输入永远是记录的连续原始字节，不是字段值拼回去——分隔符被吃掉后拼回必错。
struct DigestSpec
{
    bool enabled = false;
    std::string alg;     // 内置算子名；为空表示走 script
    std::string script;  // Lua 函数体（ctx.data / ctx.len / ctx.fields / ctx.raw）
    std::string from;    // "after:<段名>" | "frame_start"（缺省 frame_start）
    std::string to;      // "before:<段名>" | "frame_end"（缺省 before:self）
    bool hasInject = false;   // 追加常量（MAVLink CRC_EXTRA）：等价于在数据尾部再累加一个字节
    uint32_t inject = 0;
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
    BitSpec bit;
    ExprPtr cond; // 段级 if：条件不成立时该段不存在（不消耗字节、不出通道）
    EmitMode emitMode = EmitMode::Channel;
    ExprPtr expectExpr; // 非空即常量匹配
    std::vector<CheckSpec> checks;
    LengthSpec length;

    // ---- v3 ----
    bool countWildcard = false;  // count:"*"：本段读到帧尾（全局唯一，其后只允许固定尾部段）
    std::string blockRef;        // type 指向 blocks 里的块名，编译期展开
    ExprPtr transformExpr;       // transform 写成非线性表达式时逐帧求值；线性形式折进 scale/offset

    // ---- text 词法（SPEC §7）：段的边界不再来自字节宽度，而来自定界符/字面量 ----
    std::string literal;                 // 字面量段：必须精确匹配，不产通道（如 "$"、"*"）
    std::string key;                     // key=value 词法：段的键名（如 "lat"），值是键后到分隔符/记录尾
    bool hasDelimiter = false;
    std::vector<std::string> delimiter;  // 候选定界符集合；"blank" 表示空格/Tab 字符类
    std::string repeater;                // ""（单个）| "*"（重复到下一段边界/记录尾）
    bool hasWidth = false;               // fixed 词法：定宽字符数
    uint32_t width = 0;
    bool isRest = false;                 // 整块原文，不切分（CSV 引号字段、NMEA payload 原文）
    int64_t base = 10;                   // 数值进制，缺省 10
    bool hasFixedDigits = false;         // 数值小数位数（校验用）
    uint32_t fixedDigits = 0;
    bool signedFlag = false;             // 允许负号
    bool sci = false;                    // 允许科学计数法
    std::string as;                      // 覆盖输出类型名（u8/i16/f32...）
    std::string emptyPolicy;             // ""（空串报错）| "nan" | "skip" | "raw"
    std::string pattern;                 // 正则只做校验，不切分
    std::string onMismatch;              // "" | "reject" | "warn" | "skip"
    // 派生字段：不从字节/文本读，由表达式算出（引用前面已解析的段），不占空间
    ExprPtr derive;

    // 二进制变长数组的终止条件（text 用 repeater:"*"）：
    //   "eos"   → 读到帧尾
    //   "until" → 每读一个元素求值 repeatUntil，为真即停
    std::string repeatMode;
    ExprPtr repeatUntil;
    bool hasRange = false;                // 值域：越界按 on_mismatch 处理
    double rangeMin = 0.0;
    double rangeMax = 0.0;
    bool hasEnum = false;                 // 合法取值集合（超集外按 on_mismatch）
    std::vector<double> enumValues;
    std::vector<TransformStep> transform;
    DigestSpec digest;
};

// 块（v3）：一组字段的整体，段用 type 引用块名。
//   无 of = 顺序块：按自身顺序读字节，可配合 count:"*" 整体重复；
//   有 of = 位域块：先读一个 of 类型的容器值，再按 fields 切位（偏移相对容器起点）。
// 块只允许一层：fields 里不再引用别的块。
struct BlockDef
{
    std::string name;
    std::string of;
    std::string description;
    std::vector<SegmentDef> fields;
};

// 派生字段（v3 顶层 derived）：不读字节，按序用已解析的段/前序派生求值。
struct DerivedDef
{
    std::string name;
    ExprPtr expr;
    std::string unit;
    std::string description;
    EmitMode emitMode = EmitMode::Channel;
};

// 类型别名（对应 DBC SGTYPE）：段写 type:"imu" 时展开成这里的类型/换算/值域。
struct AliasDef
{
    TypeInfo ti;
    double scale = 1.0;
    double offset = 0.0;
    std::string unit;
    bool hasRange = false;
    double rangeMin = 0.0;
    double rangeMax = 0.0;
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
    // 帧尾候选集合（如 {"\r\n", "\n"}）。声明了就硬校验，没声明就不管：
    // 有 literal 段时靠 literal 定位分帧，没 literal 时才用它分帧。
    bool hasTerminator = false;
    std::vector<std::string> terminator;
    std::string version = "1";
    std::string name = "frame";
    std::string description;
    std::string layout = "sequential";
    std::string mode = "bin"; // bin=按字节解释；text=按字符串切 token。必填，无 mixed
    std::string endian = "little";
    uint32_t maxsize = 4096;
    std::vector<SegmentDef> segments;
    std::map<std::string, BlockDef> blocks; // v3：可被段引用的字段组
    std::vector<DerivedDef> derived;        // v3：顶层派生字段，按序求值
    std::map<std::string, AliasDef> aliases;
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

// 段的边界类型：这是唯一的分类维度，不存在"整份协议是 text 还是二进制"。
//   Byte     → 边界由 type 宽度 × count 推出（位域段不推进游标）
//   Literal  → 逐字符/逐字节匹配固定字面量（"$"、"*"、0xAA 用 expect 表达）
//   Delimit  → 读到定界符（含 blank 字符类）
//   Fixed    → 定宽字符
//   Rest     → 直到记录尾
//   Repeat   → 重复到后一个段边界
// 同一记录里允许混排（文本头 + 二进制载荷）。
enum class SegBoundary
{
    Byte = 0,
    Literal,
    Delimit,
    Fixed,
    Rest,
    Repeat,
    Keyed
};

inline SegBoundary boundaryOf(const SegmentDef &s)
{
    if (!s.literal.empty())
        return SegBoundary::Literal;
    if (!s.key.empty())
        return SegBoundary::Keyed;
    if (s.isRest)
        return SegBoundary::Rest;
    if (s.hasWidth)
        return SegBoundary::Fixed;
    if (s.hasDelimiter)
        return SegBoundary::Delimit;
    if (!s.repeater.empty())
        return SegBoundary::Repeat;
    return SegBoundary::Byte;
}

inline bool isTextSegment(const SegmentDef &s) { return boundaryOf(s) != SegBoundary::Byte; }

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
