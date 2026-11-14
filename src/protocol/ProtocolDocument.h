#pragma once

#include <QJsonObject>

#include <cstdint>
#include <map>
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
    std::string description;
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

struct EvalCtx
{
    std::map<std::string, std::vector<double>> values;
    std::map<std::string, std::string> strings;

    bool get(const std::string &n, int index, double &out, std::string &err) const
    {
        auto it = values.find(n);
        if (it == values.end())
        {
            err = "表达式引用未知字段: " + n;
            return false;
        }
        if (index < 0)
        {
            if (it->second.size() != 1)
            {
                err = "字段 " + n + " 是数组，需要下标";
                return false;
            }
            out = it->second[0];
            return true;
        }
        if (index >= static_cast<int>(it->second.size()))
        {
            err = "字段 " + n + " 下标越界";
            return false;
        }
        out = it->second[static_cast<size_t>(index)];
        return true;
    }
};

bool evalExpr(const ExprPtr &e, const EvalCtx &ctx, double &out, std::string &err);

/// 解析协议 JSON 为唯一类型化文档；失败返回 false 并写 err。
bool parseDocument(const QJsonObject &root, ProtocolDocument &doc, std::string &err);

/// 把类型化文档写回协议 JSON；用于编辑器和保存的唯一序列化入口。
QJsonObject toJson(const ProtocolDocument &doc);

/// 表达式文本化，供编辑器表格显示和 JSON 回写。
std::string exprText(const ExprPtr &e);

} // namespace ps::protocol
