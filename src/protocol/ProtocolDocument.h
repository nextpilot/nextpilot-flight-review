#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ps::protocol
{

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr
{
    enum Kind { Num, Ref, Bin, Un, Fn } kind = Num;
    double num = 0.0;
    std::string name;
    std::string fn;
    int op = 0;
    ExprPtr a, b, c;
};

enum class FType { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, BOOL, BCD, STR };

struct TypeInfo
{
    FType type = FType::U8;
    uint32_t size = 1;
    bool sign = false;
    bool fp = false;
    bool bcd = false;
};

struct CheckSpec
{
    std::string op = "eq";
    ExprPtr rhs;
    ExprPtr rhs2;
    ExprPtr rhs3;
    std::vector<double> values;
    std::vector<std::string> over;
    std::string scope = "element";
    uint32_t poly = 0;
    uint32_t init = 0;
    uint32_t xorout = 0;
    bool refin = false;
    bool refout = false;
    int width = 0;
};

struct CountSpec
{
    bool enabled = false;
    std::vector<std::string> fields;
    std::string unit = "elements";
    ExprPtr expr;
};

struct SegmentDef
{
    std::string name;
    std::string role = "data";
    std::string unit;
    std::string prefix;
    TypeInfo type;
    ExprPtr sizeExpr;
    double scale = 1.0;
    double offset = 0.0;
    bool bigEndian = false;
    bool hidden = false;
    bool plot = true;
    bool hasValue = false;
    ExprPtr valueExpr;
    std::vector<CheckSpec> checks;
    CountSpec counts;
    bool fromVariant = false;
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

} // namespace ps::protocol
