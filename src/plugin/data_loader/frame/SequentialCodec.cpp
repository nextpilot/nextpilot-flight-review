#include "SequentialCodec.hpp"
#include "PsLoaderCommon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

/* ---------------- 表达式 ---------------- */

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

class ExprParser
{
public:
    ExprParser(const std::string &text, std::string &err) : m_s(text), m_err(err) {}

    ExprPtr parse()
    {
        skip();
        ExprPtr e = parseBin(0, 0);
        skip();
        if (!e || m_pos != m_s.size())
            m_err = "表达式语法错误";
        return e;
    }

private:
    const std::string &m_s;
    std::string &m_err;
    size_t m_pos = 0;
    int m_depth = 0;

    void skip()
    {
        while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t'))
            ++m_pos;
    }

    static int precedence(int op)
    {
        switch (op)
        {
        case '|': return 1;
        case '^': return 2;
        case '&': return 3;
        case 100: case 101: return 4; // << >>
        case '+': case '-': return 5;
        case '*': case '/': case '%': return 6;
        default: return -1;
        }
    }

    bool readOp(int &op)
    {
        skip();
        if (m_pos + 1 < m_s.size())
        {
            if (m_s[m_pos] == '<' && m_s[m_pos + 1] == '<') { op = 100; m_pos += 2; return true; }
            if (m_s[m_pos] == '>' && m_s[m_pos + 1] == '>') { op = 101; m_pos += 2; return true; }
        }
        if (m_pos < m_s.size() && std::strchr("+-*/%&|^", m_s[m_pos]))
        {
            op = m_s[m_pos++];
            return true;
        }
        return false;
    }

    ExprPtr parseBin(int minPrec, int depth)
    {
        if (++depth > 32)
        {
            m_err = "表达式嵌套过深";
            return nullptr;
        }
        ExprPtr left = parseUnary(depth);
        if (!left)
            return nullptr;
        while (true)
        {
            int op = 0;
            size_t save = m_pos;
            if (!readOp(op) || precedence(op) < minPrec)
            {
                m_pos = save;
                break;
            }
            int prec = precedence(op);
            ExprPtr right = parseBin(prec + 1, depth);
            if (!right)
                return nullptr;
            ExprPtr n = std::make_shared<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->a = left;
            n->b = right;
            left = n;
        }
        return left;
    }

    ExprPtr parseUnary(int depth)
    {
        skip();
        if (m_pos < m_s.size() && m_s[m_pos] == '-')
        {
            ++m_pos;
            ExprPtr v = parseUnary(depth + 1);
            if (!v)
                return nullptr;
            ExprPtr n = std::make_shared<Expr>();
            n->kind = Expr::Un;
            n->op = '-';
            n->a = v;
            return n;
        }
        return parsePrimary(depth);
    }

    ExprPtr parsePrimary(int depth)
    {
        skip();
        if (m_pos >= m_s.size())
        {
            m_err = "表达式意外结束";
            return nullptr;
        }
        if (m_s[m_pos] == '(')
        {
            ++m_pos;
            ExprPtr e = parseBin(0, depth + 1);
            skip();
            if (!e || m_pos >= m_s.size() || m_s[m_pos] != ')')
            {
                m_err = "表达式缺少右括号";
                return nullptr;
            }
            ++m_pos;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(m_s[m_pos])))
        {
            size_t start = m_pos;
            if (m_s[m_pos] == '0' && m_pos + 1 < m_s.size() && (m_s[m_pos + 1] == 'x' || m_s[m_pos + 1] == 'X'))
            {
                m_pos += 2;
                while (m_pos < m_s.size() && std::isxdigit(static_cast<unsigned char>(m_s[m_pos])))
                    ++m_pos;
            }
            else
            {
                while (m_pos < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_pos])))
                    ++m_pos;
            }
            std::string tok = m_s.substr(start, m_pos - start);
            ExprPtr n = std::make_shared<Expr>();
            n->kind = Expr::Num;
            try { n->num = static_cast<double>(std::stoull(tok, nullptr, 0)); }
            catch (...) { m_err = "非法数字: " + tok; return nullptr; }
            return n;
        }
        if (std::isalpha(static_cast<unsigned char>(m_s[m_pos])) || m_s[m_pos] == '_')
        {
            size_t start = m_pos;
            while (m_pos < m_s.size() &&
                   (std::isalnum(static_cast<unsigned char>(m_s[m_pos])) || m_s[m_pos] == '_'))
                ++m_pos;
            std::string id = m_s.substr(start, m_pos - start);
            skip();
            if (m_pos < m_s.size() && m_s[m_pos] == '(')
            {
                ++m_pos;
                ExprPtr n = std::make_shared<Expr>();
                n->kind = Expr::Fn;
                n->fn = id;
                n->a = parseBin(0, depth + 1);
                if (!n->a)
                    return nullptr;
                skip();
                if (m_pos < m_s.size() && m_s[m_pos] == ',')
                {
                    ++m_pos;
                    n->b = parseBin(0, depth + 1);
                    skip();
                    if (m_pos < m_s.size() && m_s[m_pos] == ',')
                    {
                        ++m_pos;
                        n->c = parseBin(0, depth + 1);
                        skip();
                    }
                }
                if (m_pos >= m_s.size() || m_s[m_pos] != ')')
                {
                    m_err = "函数缺少右括号: " + id;
                    return nullptr;
                }
                ++m_pos;
                return n;
            }
            ExprPtr n = std::make_shared<Expr>();
            n->kind = Expr::Ref;
            n->name = id;
            skip();
            if (m_pos < m_s.size() && m_s[m_pos] == '[')
            {
                ++m_pos;
                n->b = parseBin(0, depth + 1);
                skip();
                if (!n->b || m_pos >= m_s.size() || m_s[m_pos] != ']')
                {
                    m_err = "数组下标缺少右括号";
                    return nullptr;
                }
                ++m_pos;
            }
            return n;
        }
        m_err = "表达式非法字符";
        return nullptr;
    }
};

bool evalExpr(const ExprPtr &e, const EvalCtx &ctx, double &out, std::string &err)
{
    if (!e)
    {
        err = "空表达式";
        return false;
    }
    switch (e->kind)
    {
    case Expr::Num:
        out = e->num;
        return true;
    case Expr::Ref:
    {
        int idx = -1;
        if (e->b)
        {
            double iv = 0;
            if (!evalExpr(e->b, ctx, iv, err))
                return false;
            idx = static_cast<int>(iv);
        }
        return ctx.get(e->name, idx, out, err);
    }
    case Expr::Un:
    {
        double v = 0;
        if (!evalExpr(e->a, ctx, v, err))
            return false;
        out = -v;
        return true;
    }
    case Expr::Fn:
    {
        double a = 0;
        if (!evalExpr(e->a, ctx, a, err))
            return false;
        if (e->fn == "abs")
        {
            out = std::fabs(a);
            return true;
        }
        double b = 0;
        if (!e->b)
        {
            err = "函数参数不足: " + e->fn;
            return false;
        }
        if (!evalExpr(e->b, ctx, b, err))
            return false;
        if (e->fn == "min") { out = std::min(a, b); return true; }
        if (e->fn == "max") { out = std::max(a, b); return true; }
        if (e->fn == "clamp")
        {
            double c = 0;
            if (!e->c || !evalExpr(e->c, ctx, c, err))
            {
                err = "clamp 需要 3 个参数";
                return false;
            }
            out = std::min(std::max(a, b), c);
            return true;
        }
        err = "未知函数: " + e->fn;
        return false;
    }
    case Expr::Bin:
    {
        double a = 0, b = 0;
        if (!evalExpr(e->a, ctx, a, err) || !evalExpr(e->b, ctx, b, err))
            return false;
        switch (e->op)
        {
        case '+': out = a + b; return true;
        case '-': out = a - b; return true;
        case '*': out = a * b; return true;
        case '/':
            if (b == 0) { err = "表达式除零"; return false; }
            out = std::trunc(a / b);
            return true;
        case '%':
            if (b == 0) { err = "表达式取模零"; return false; }
            out = std::fmod(a, b);
            return true;
        case '&': out = double(static_cast<int64_t>(a) & static_cast<int64_t>(b)); return true;
        case '|': out = double(static_cast<int64_t>(a) | static_cast<int64_t>(b)); return true;
        case '^': out = double(static_cast<int64_t>(a) ^ static_cast<int64_t>(b)); return true;
        case 100: out = double(static_cast<int64_t>(a) << static_cast<int64_t>(b)); return true;
        case 101: out = double(static_cast<int64_t>(a) >> static_cast<int64_t>(b)); return true;
        default: err = "未知运算符"; return false;
        }
    }
    }
    err = "表达式求值失败";
    return false;
}

ExprPtr parseExprText(const std::string &text, std::string &err)
{
    ExprParser p(text, err);
    return p.parse();
}

ExprPtr jsonExpr(const QJsonValue &v, std::string &err)
{
    if (v.isDouble())
    {
        ExprPtr n = std::make_shared<Expr>();
        n->kind = Expr::Num;
        n->num = v.toDouble();
        return n;
    }
    if (v.isString())
        return parseExprText(v.toString().toStdString(), err);
    err = "表达式必须是数字或字符串";
    return nullptr;
}

/* ---------------- 模板 ---------------- */

enum class FType { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, BOOL, BCD, STR };

struct TypeInfo
{
    FType type = FType::U8;
    uint32_t size = 1;
    bool sign = false;
    bool fp = false;
    bool bcd = false;
};

bool parseType(const std::string &raw, TypeInfo &ti, std::string &err)
{
    std::string t = raw;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    if (t == "u8" || t == "uint8" || t == "byte") { ti = {FType::U8, 1, false, false, false}; return true; }
    if (t == "i8" || t == "int8") { ti = {FType::I8, 1, true, false, false}; return true; }
    if (t == "u16" || t == "uint16") { ti = {FType::U16, 2, false, false, false}; return true; }
    if (t == "i16" || t == "int16") { ti = {FType::I16, 2, true, false, false}; return true; }
    if (t == "u32" || t == "uint32") { ti = {FType::U32, 4, false, false, false}; return true; }
    if (t == "i32" || t == "int32") { ti = {FType::I32, 4, true, false, false}; return true; }
    if (t == "u64" || t == "uint64") { ti = {FType::U64, 8, false, false, false}; return true; }
    if (t == "i64" || t == "int64") { ti = {FType::I64, 8, true, false, false}; return true; }
    if (t == "f32" || t == "float" || t == "float32") { ti = {FType::F32, 4, true, true, false}; return true; }
    if (t == "f64" || t == "double" || t == "float64") { ti = {FType::F64, 8, true, true, false}; return true; }
    if (t == "bool" || t == "boolean") { ti = {FType::BOOL, 1, false, false, false}; return true; }
    if (t == "bcd") { ti = {FType::BCD, 1, false, false, true}; return true; }
    if (t == "str" || t == "string" || t == "ascii" || t == "char")
    { ti = {FType::STR, 1, false, false, false}; return true; }
    err = "未知类型: " + raw;
    return false;
}

inline std::string numberKey(double v)
{
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return "n:" + os.str();
}

inline std::string stringKey(const std::string &v) { return "s:" + v; }

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

struct SegSpec
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

struct VariantSpec
{
    std::vector<std::string> keys;
    std::string name;
    std::vector<SegSpec> segments;
};

struct SeriesSpec
{
    bool enabled = false;
    ExprPtr selector;
    std::string payload;
    std::string fallback = "reject";
    std::vector<VariantSpec> variants;
};

struct TemplateSpec
{
    std::string name;
    std::string endian = "little";
    uint32_t maxsize = 4096;
    std::vector<SegSpec> segments;
    SeriesSpec series;
    bool hasTimestamp = false;
    ExprPtr timestampExpr;
    std::string timestampUnit = "s";
    std::string timestampOrigin = "first_frame";
    bool timestampRaw = false;
    std::string timestampFallback = "reject";
};

bool parseChecks(const QJsonArray &arr, std::vector<CheckSpec> &out, std::string &err)
{
    for (const QJsonValue &vv : arr)
    {
        const QJsonObject o = vv.toObject();
        CheckSpec c;
        c.op = o.value(QStringLiteral("op")).toString(QStringLiteral("eq")).toStdString();
        c.scope = o.value(QStringLiteral("scope")).toString(QStringLiteral("element")).toStdString();
        if (o.contains(QStringLiteral("value")) && o.contains(QStringLiteral("expr")))
        {
            err = "check 的 value/expr 不能同时出现";
            return false;
        }
        if (o.contains(QStringLiteral("value")))
        {
            const QJsonValue v = o.value(QStringLiteral("value"));
            if (v.isString())
            {
                c.rhs = parseExprText(v.toString().toStdString(), err);
                if (!c.rhs) return false;
            }
            else
            {
                c.rhs = jsonExpr(v, err);
                if (!c.rhs) return false;
            }
        }
        else if (o.contains(QStringLiteral("expr")))
        {
            c.rhs = jsonExpr(o.value(QStringLiteral("expr")), err);
            if (!c.rhs) return false;
        }

        if (c.op == "range")
        {
            c.rhs = jsonExpr(o.value(QStringLiteral("min")), err);
            if (!c.rhs) return false;
            c.rhs2 = jsonExpr(o.value(QStringLiteral("max")), err);
            if (!c.rhs2) return false;
        }
        else if (c.op == "mask")
        {
            c.rhs = jsonExpr(o.value(QStringLiteral("and")), err);
            if (!c.rhs) return false;
            c.rhs2 = jsonExpr(o.value(QStringLiteral("eq")), err);
            if (!c.rhs2) return false;
        }
        else if (c.op == "in")
        {
            for (const QJsonValue &iv : o.value(QStringLiteral("values")).toArray())
                c.values.push_back(iv.toDouble());
        }
        else if (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" ||
                 c.op == "sum8" || c.op == "sum16" || c.op == "xor8" || c.op == "parity")
        {
            if (c.op == "crc8") { c.width = 1; c.poly = 0x07; c.init = 0x00; }
            if (c.op == "crc16") { c.width = 2; c.poly = 0x1021; c.init = 0xFFFF; }
            if (c.op == "crc32") { c.width = 4; c.poly = 0x04C11DB7; c.init = 0xFFFFFFFF; c.refin = true; c.refout = true; c.xorout = 0xFFFFFFFF; }
            if (c.op == "sum8") c.width = 1;
            if (c.op == "sum16") c.width = 2;
            if (c.op == "xor8") c.width = 1;
            if (c.op == "parity") c.width = 1;
            if (o.contains(QStringLiteral("poly")))
                c.poly = static_cast<uint32_t>(std::stoul(o.value(QStringLiteral("poly")).toString().toStdString(), nullptr, 0));
            if (o.contains(QStringLiteral("init")))
                c.init = static_cast<uint32_t>(std::stoul(o.value(QStringLiteral("init")).toString().toStdString(), nullptr, 0));
            if (o.contains(QStringLiteral("xorout")))
                c.xorout = static_cast<uint32_t>(std::stoul(o.value(QStringLiteral("xorout")).toString().toStdString(), nullptr, 0));
            c.refin = o.value(QStringLiteral("refin")).toBool(c.refin);
            c.refout = o.value(QStringLiteral("refout")).toBool(c.refout);
            for (const QJsonValue &ov : o.value(QStringLiteral("over")).toArray())
                c.over.push_back(ov.toString().toStdString());
        }
        const bool sequenceOp = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" ||
                                 c.op == "sum8" || c.op == "sum16" || c.op == "xor8" || c.op == "parity" ||
                                 c.op == "in");
        if (!sequenceOp && c.rhs == nullptr)
        {
            err = "check 缺少比较值";
            return false;
        }
        out.push_back(c);
    }
    return true;
}

bool parseSegment(const QJsonObject &o, const std::string &defaultEndian, SegSpec &seg, std::string &err)
{
    seg.name = o.value(QStringLiteral("name")).toString().toStdString();
    if (seg.name.empty())
    {
        err = "segment 缺少 name";
        return false;
    }
    if (!o.contains(QStringLiteral("size")))
    {
        err = "segment " + seg.name + " 缺少 size";
        return false;
    }
    seg.sizeExpr = jsonExpr(o.value(QStringLiteral("size")), err);
    if (!seg.sizeExpr)
        return false;
    if (!parseType(o.value(QStringLiteral("type")).toString(QStringLiteral("u8")).toStdString(), seg.type, err))
        return false;

    const std::string endian = o.value(QStringLiteral("endian")).toString(QString::fromStdString(defaultEndian)).toStdString();
    seg.bigEndian = (endian == "big" || endian == "be");
    seg.scale = o.value(QStringLiteral("scale")).toDouble(1.0);
    seg.offset = o.value(QStringLiteral("offset")).toDouble(0.0);
    seg.role = o.value(QStringLiteral("role")).toString(QStringLiteral("data")).toStdString();
    seg.hidden = o.value(QStringLiteral("hidden")).toBool(false);
    seg.plot = o.value(QStringLiteral("plot")).toBool(true);
    seg.unit = o.value(QStringLiteral("unit")).toString().toStdString();
    if (o.contains(QStringLiteral("value")))
    {
        seg.hasValue = true;
        seg.valueExpr = jsonExpr(o.value(QStringLiteral("value")), err);
        if (!seg.valueExpr)
            return false;
    }
    if (o.contains(QStringLiteral("checks")) && o.value(QStringLiteral("checks")).isArray())
    {
        if (!parseChecks(o.value(QStringLiteral("checks")).toArray(), seg.checks, err))
            return false;
    }
    if (o.contains(QStringLiteral("counts")))
    {
        const QJsonValue cv = o.value(QStringLiteral("counts"));
        if (cv.isObject())
        {
            const QJsonObject co = cv.toObject();
            seg.counts.enabled = true;
            for (const QJsonValue &fv : co.value(QStringLiteral("fields")).toArray())
                seg.counts.fields.push_back(fv.toString().toStdString());
            seg.counts.unit = co.value(QStringLiteral("unit")).toString(QStringLiteral("elements")).toStdString();
            if (co.contains(QStringLiteral("expr")))
            {
                seg.counts.expr = jsonExpr(co.value(QStringLiteral("expr")), err);
                if (!seg.counts.expr)
                    return false;
            }
        }
        else if (cv.isString())
        {
            seg.counts.enabled = true;
            seg.counts.expr = jsonExpr(cv, err);
            if (!seg.counts.expr)
                return false;
        }
    }
    return true;
}

bool parseTemplate(const QJsonObject &root, TemplateSpec &t, std::string &err)
{
    t.name = root.value(QStringLiteral("name")).toString(QStringLiteral("frame")).toStdString();
    t.endian = root.value(QStringLiteral("endian")).toString(QStringLiteral("little")).toStdString();
    t.maxsize = static_cast<uint32_t>(root.value(QStringLiteral("maxsize")).toInt(4096));
    if (t.maxsize < 2)
        t.maxsize = 4096;

    if (root.contains(QStringLiteral("timestamp")))
    {
        QJsonObject to = root.value(QStringLiteral("timestamp")).toObject();
        t.hasTimestamp = true;
        t.timestampExpr = jsonExpr(to.value(QStringLiteral("expr")), err);
        if (!t.timestampExpr)
            return false;
        t.timestampUnit = to.value(QStringLiteral("unit")).toString(QStringLiteral("s")).toStdString();
        t.timestampOrigin = to.value(QStringLiteral("origin")).toString(QStringLiteral("first_frame")).toStdString();
        t.timestampRaw = to.value(QStringLiteral("raw")).toBool(false);
        t.timestampFallback = to.value(QStringLiteral("fallback")).toString(QStringLiteral("reject")).toStdString();
    }

    for (const QJsonValue &sv : root.value(QStringLiteral("segments")).toArray())
    {
        SegSpec seg;
        if (!parseSegment(sv.toObject(), t.endian, seg, err))
            return false;
        t.segments.push_back(std::move(seg));
    }
    if (t.segments.empty())
    {
        err = "segments 为空";
        return false;
    }

    if (root.contains(QStringLiteral("series")))
    {
        const QJsonObject so = root.value(QStringLiteral("series")).toObject();
        t.series.enabled = true;
        t.series.selector = jsonExpr(so.value(QStringLiteral("selector")), err);
        if (!t.series.selector)
            return false;
        t.series.payload = so.value(QStringLiteral("payload")).toString().toStdString();
        t.series.fallback = so.value(QStringLiteral("fallback")).toString(QStringLiteral("reject")).toStdString();
        for (const QJsonValue &vv : so.value(QStringLiteral("variants")).toArray())
        {
            const QJsonObject vo = vv.toObject();
            VariantSpec var;
            var.name = vo.value(QStringLiteral("name")).toString().toStdString();
            if (vo.contains(QStringLiteral("value")))
            {
                const QJsonValue &v = vo.value(QStringLiteral("value"));
                var.keys.push_back(v.isString() ? stringKey(v.toString().toStdString())
                                                : numberKey(v.toDouble()));
            }
            for (const QJsonValue &iv : vo.value(QStringLiteral("values")).toArray())
                var.keys.push_back(iv.isString() ? stringKey(iv.toString().toStdString())
                                                 : numberKey(iv.toDouble()));
            for (const QJsonValue &fv : vo.value(QStringLiteral("segments")).toArray())
            {
                SegSpec seg;
                if (!parseSegment(fv.toObject(), t.endian, seg, err))
                    return false;
                seg.fromVariant = true;
                seg.prefix = var.name + ".";
                var.segments.push_back(std::move(seg));
            }
            if (var.keys.empty())
            {
                err = "variant " + var.name + " 缺少 value/values";
                return false;
            }
            t.series.variants.push_back(std::move(var));
        }
        if (t.series.variants.empty())
        {
            err = "series.variants 为空";
            return false;
        }
    }
    return true;
}

/* ---------------- 解码与校验 ---------------- */

uint64_t readUInt(const uint8_t *p, uint32_t n, bool be)
{
    uint64_t v = 0;
    if (be)
    {
        for (uint32_t i = 0; i < n; ++i)
            v = (v << 8) | p[i];
    }
    else
    {
        for (uint32_t i = 0; i < n; ++i)
            v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

double readElem(const uint8_t *p, const TypeInfo &ti, bool be)
{
    switch (ti.type)
    {
    case FType::U8: return double(p[0]);
    case FType::STR: return double(p[0]);
    case FType::I8: return double(static_cast<int8_t>(p[0]));
    case FType::U16: return double(readUInt(p, 2, be));
    case FType::I16:
    {
        uint16_t raw = static_cast<uint16_t>(readUInt(p, 2, be));
        return double(static_cast<int16_t>(raw));
    }
    case FType::U32: return double(readUInt(p, 4, be));
    case FType::I32:
    {
        uint32_t raw = static_cast<uint32_t>(readUInt(p, 4, be));
        return double(static_cast<int32_t>(raw));
    }
    case FType::U64: return double(readUInt(p, 8, be));
    case FType::I64:
    {
        uint64_t raw = readUInt(p, 8, be);
        return double(static_cast<int64_t>(raw));
    }
    case FType::F32:
    {
        uint32_t raw = static_cast<uint32_t>(readUInt(p, 4, be));
        float f = 0;
        std::memcpy(&f, &raw, sizeof(f));
        return double(f);
    }
    case FType::F64:
    {
        uint64_t raw = readUInt(p, 8, be);
        double d = 0;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }
    case FType::BOOL: return p[0] ? 1.0 : 0.0;
    case FType::BCD:
    {
        uint8_t b = p[0];
        return double((b >> 4) * 10 + (b & 0x0F));
    }
    }
    return 0;
}

uint32_t reflectBits(uint32_t v, int width)
{
    uint32_t r = 0;
    for (int i = 0; i < width; ++i)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

uint32_t crcGeneric(const uint8_t *data, size_t n, uint32_t poly, uint32_t init,
                    bool refin, bool refout, uint32_t xorout, int width)
{
    const uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    const uint32_t top = 1u << (width - 1);
    uint32_t crc = init & mask;
    for (size_t i = 0; i < n; ++i)
    {
        if (refin)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc & 1u) ? ((crc >> 1) ^ reflectBits(poly, width)) : (crc >> 1);
        }
        else
        {
            crc ^= static_cast<uint32_t>(data[i]) << (width - 8);
            for (int b = 0; b < 8; ++b)
                crc = (crc & top) ? ((crc << 1) ^ poly) : (crc << 1);
        }
        crc &= mask;
    }
    if (refout != refin)
        crc = reflectBits(crc, width);
    return (crc ^ xorout) & mask;
}

uint32_t sequenceValue(const CheckSpec &c, const std::vector<uint8_t> &buf)
{
    if (c.op == "sum8" || c.op == "sum16")
    {
        uint64_t sum = 0;
        for (uint8_t b : buf)
            sum += b;
        return static_cast<uint32_t>(c.op == "sum8" ? (sum & 0xFF) : (sum & 0xFFFF));
    }
    if (c.op == "xor8")
    {
        uint8_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return x;
    }
    if (c.op == "parity")
    {
        uint8_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return (x & 1u) ? 1u : 0u;
    }
    return crcGeneric(buf.data(), buf.size(), c.poly, c.init, c.refin, c.refout, c.xorout,
                      c.width > 0 ? c.width * 8 : 16);
}

struct RtSeg
{
    const SegSpec *spec = nullptr;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    uint32_t count = 0;
    std::vector<double> raw;
    std::vector<double> phys;
};

bool evalSize(const SegSpec &seg, const EvalCtx &ctx, uint32_t &count, uint32_t &bytes,
              std::string &err)
{
    double v = 0;
    if (!evalExpr(seg.sizeExpr, ctx, v, err))
        return false;
    if (!(v > 0) || v != std::trunc(v) || v > 1000000)
    {
        err = "段 " + seg.name + " size 非法";
        return false;
    }
    count = static_cast<uint32_t>(v);
    bytes = count * seg.type.size;
    if (bytes == 0 || bytes > 1u << 20)
    {
        err = "段 " + seg.name + " 字节数非法";
        return false;
    }
    return true;
}

bool checkFrame(const std::vector<RtSeg> &fields, const std::map<std::string, RtSeg *> &byName,
                const std::map<std::string, std::pair<uint32_t, uint32_t>> &pseudo,
                const std::vector<std::string> &order, const uint8_t *frame,
                const EvalCtx &rawCtx, std::string &err)
{
    for (const RtSeg &rt : fields)
    {
        const SegSpec &seg = *rt.spec;
        if (seg.hasValue)
        {
            double want = 0;
            if (!evalExpr(seg.valueExpr, rawCtx, want, err))
                return false;
            for (double v : rt.raw)
                if (v != want)
                {
                    err = "段 " + seg.name + " 常量不匹配";
                    return false;
                }
        }

        for (const CheckSpec &c : seg.checks)
        {
            const bool seqOp = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" ||
                                c.op == "sum8" || c.op == "sum16" || c.op == "xor8" || c.op == "parity");
            if (seqOp)
            {
                if (rt.raw.size() != 1 || rt.bytes != static_cast<uint32_t>(c.width))
                {
                    err = "段 " + seg.name + " 校验宽度与算子不匹配";
                    return false;
                }
                std::vector<uint8_t> buf;
                std::set<std::string> seen;
                for (const std::string &token : c.over)
                {
                    std::vector<std::string> names;
                    const size_t colon = token.find(':');
                    if (colon != std::string::npos)
                    {
                        const std::string a = token.substr(0, colon);
                        const std::string b = token.substr(colon + 1);
                        auto ia = std::find(order.begin(), order.end(), a);
                        auto ib = std::find(order.begin(), order.end(), b);
                        if (ia == order.end() || ib == order.end() || ia > ib)
                        {
                            err = "over 区间非法: " + token;
                            return false;
                        }
                        names.assign(ia, ib + 1);
                    }
                    else
                    {
                        names.push_back(token);
                    }
                    for (const std::string &n : names)
                    {
                        if (!seen.insert(n).second)
                            continue;
                        auto pit = pseudo.find(n);
                        if (pit != pseudo.end())
                        {
                            const uint8_t *p = frame + pit->second.first;
                            buf.insert(buf.end(), p, p + pit->second.second);
                            continue;
                        }
                        auto it = byName.find(n);
                        if (it == byName.end())
                        {
                            err = "over 引用未知段: " + n;
                            return false;
                        }
                        const uint8_t *p = frame + it->second->offset;
                        buf.insert(buf.end(), p, p + it->second->bytes);
                    }
                }
                const uint32_t got = sequenceValue(c, buf);
                const double want = rt.raw[0];
                if (static_cast<uint32_t>(want) != got)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
                continue;
            }

            for (double v : rt.raw)
            {
                bool ok = false;
                double rhs = 0;
                if (c.op == "eq")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v == rhs;
                else if (c.op == "ne")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v != rhs;
                else if (c.op == "lt")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v < rhs;
                else if (c.op == "le")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v <= rhs;
                else if (c.op == "gt")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v > rhs;
                else if (c.op == "ge")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v >= rhs;
                else if (c.op == "in")
                {
                    for (double iv : c.values)
                        if (v == iv) { ok = true; break; }
                }
                else if (c.op == "range")
                {
                    double lo = 0, hi = 0;
                    ok = evalExpr(c.rhs, rawCtx, lo, err) &&
                         evalExpr(c.rhs2, rawCtx, hi, err) && v >= lo && v <= hi;
                }
                else if (c.op == "mask")
                {
                    double m = 0, expected = 0;
                    ok = evalExpr(c.rhs, rawCtx, m, err) &&
                         evalExpr(c.rhs2, rawCtx, expected, err) &&
                         (static_cast<int64_t>(v) & static_cast<int64_t>(m)) == static_cast<int64_t>(expected);
                }
                else
                {
                    err = "未知校验算子: " + c.op;
                    return false;
                }
                if (!ok)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
            }
        }
    }
    return true;
}

bool matchVariant(const SeriesSpec &series, const std::string &selectorKey,
                  const VariantSpec *&variant, std::string &err)
{
    variant = nullptr;
    for (const VariantSpec &v : series.variants)
        for (const std::string &id : v.keys)
            if (id == selectorKey)
            {
                variant = &v;
                return true;
            }
    return true;
}

} // namespace

bool looksSequentialProtocol(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    return root.contains(QStringLiteral("segments"));
}

int32_t decodeSequentialFrames(const QByteArray &bytes, const QByteArray &json,
                               const PsHostApi *host, void **dataset,
                               char *err, uint32_t errCap)
{
    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(json, &perr).object();
    if (perr.error != QJsonParseError::NoError)
    {
        psWriteErr(err, errCap, "协议 JSON 解析失败: " + perr.errorString().toStdString());
        return 2;
    }
    TemplateSpec tpl;
    std::string e;
    if (!parseTemplate(root, tpl, e))
    {
        psWriteErr(err, errCap, e);
        return 2;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    const size_t total = static_cast<size_t>(bytes.size());
    std::map<std::string, PsChannelData> chans;
    size_t off = 0;
    double frameIndex = 0;
    bool haveBase = false;
    double baseTs = 0;
    uint64_t rejected = 0;
    uint64_t warnings = 0;

    while (off + 1 < total)
    {
        EvalCtx rawCtx;
        EvalCtx physCtx;
        std::vector<RtSeg> fields;
        std::map<std::string, RtSeg *> byName;
        std::map<std::string, std::pair<uint32_t, uint32_t>> pseudo;
        std::vector<std::string> order;
        for (const SegSpec &s : tpl.segments)
            order.push_back(s.name);

        uint32_t pos = 0;
        bool payloadSeen = false;
        bool variantHit = false;
        uint32_t payloadOff = 0, payloadBytes = 0, payloadCount = 0;
        bool ok = true;
        for (const SegSpec &seg : tpl.segments)
        {
            uint32_t count = 0, nbytes = 0;
            if (!evalSize(seg, rawCtx, count, nbytes, e))
            {
                ok = false;
                break;
            }
            if (tpl.series.enabled && seg.role == "payload")
            {
                payloadSeen = true;
                payloadOff = pos;
                payloadBytes = nbytes;
                payloadCount = count;
                if (static_cast<uint64_t>(off) + pos + nbytes > total)
                {
                    e = "payload 越过文件末尾";
                    ok = false;
                    break;
                }
                pos += nbytes;
                std::vector<double> pv;
                const uint8_t *p = data + off + payloadOff;
                for (uint32_t i = 0; i < nbytes; ++i)
                    pv.push_back(p[i]);
                rawCtx.values["payload"] = pv;
                physCtx.values["payload"] = pv;
                pseudo["payload"] = {payloadOff, nbytes};
                continue;
            }
            if (seg.role == "payload")
            {
                payloadSeen = true;
                payloadOff = pos;
                payloadBytes = nbytes;
                payloadCount = count;
            }
            if (static_cast<uint64_t>(pos) + nbytes + off > total)
            {
                e = "帧越过文件末尾";
                ok = false;
                break;
            }
            RtSeg rt;
            rt.spec = &seg;
            rt.offset = pos;
            rt.bytes = nbytes;
            rt.count = count;
            const uint8_t *p = data + off + pos;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint8_t *ep = p + static_cast<size_t>(i) * seg.type.size;
                rt.raw.push_back(seg.type.bcd && seg.type.size > 1
                                     ? double(readUInt(ep, seg.type.size, seg.bigEndian))
                                     : readElem(ep, seg.type, seg.bigEndian));
            }
            rawCtx.values[seg.name] = rt.raw;
            if (seg.type.type == FType::STR)
            {
                std::string txt;
                for (uint32_t i = 0; i < nbytes; ++i)
                    txt.push_back(static_cast<char>(p[i]));
                rawCtx.strings[seg.name] = txt;
                physCtx.strings[seg.name] = txt;
            }
            for (double v : rt.raw)
                rt.phys.push_back(v * seg.scale + seg.offset);
            std::vector<double> pv;
            for (double v : rt.raw)
                pv.push_back(v * seg.scale + seg.offset);
            physCtx.values[seg.name] = pv;
            fields.push_back(std::move(rt));
            pos += nbytes;
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        if (tpl.series.enabled)
        {
            if (!payloadSeen)
            {
                e = "series.payload 未找到";
                ok = false;
            }
            else
            {
                std::string selKey;
                if (tpl.series.selector->kind == Expr::Ref && !tpl.series.selector->b &&
                    rawCtx.strings.count(tpl.series.selector->name))
                {
                    selKey = stringKey(rawCtx.strings[tpl.series.selector->name]);
                }
                else
                {
                    double sel = 0;
                    if (!evalExpr(tpl.series.selector, rawCtx, sel, e))
                        ok = false;
                    else if (sel != std::trunc(sel))
                    {
                        e = "series.selector 不是整数";
                        ok = false;
                    }
                    else
                        selKey = numberKey(sel);
                }
                const VariantSpec *var = nullptr;
                if (ok && !matchVariant(tpl.series, selKey, var, e))
                    ok = false;
                if (ok && var)
                {
                    variantHit = true;
                }
                if (ok && !var)
                {
                    if (tpl.series.fallback == "reject")
                    {
                        e = "series 未命中 msgid";
                        ok = false;
                    }
                }
                else if (ok)
                {
                    uint32_t vpos = payloadOff;
                    EvalCtx vctx = rawCtx;
                    for (const SegSpec &seg : var->segments)
                    {
                        uint32_t count = 0, nbytes = 0;
                        if (!evalSize(seg, vctx, count, nbytes, e))
                        {
                            ok = false;
                            break;
                        }
                        RtSeg rt;
                        rt.spec = &seg;
                        rt.offset = vpos;
                        rt.bytes = nbytes;
                        rt.count = count;
                        const uint8_t *p = data + off + vpos;
                        for (uint32_t i = 0; i < count; ++i)
                            rt.raw.push_back(readElem(p + static_cast<size_t>(i) * seg.type.size,
                                                      seg.type, seg.bigEndian));
                        vctx.values[seg.name] = rt.raw;
                        for (double v : rt.raw)
                            rt.phys.push_back(v * seg.scale + seg.offset);
                        std::vector<double> pv;
                        for (double v : rt.raw)
                            pv.push_back(v * seg.scale + seg.offset);
                        physCtx.values[seg.name] = pv;
                        fields.push_back(std::move(rt));
                        vpos += nbytes;
                    }
                    if (ok && vpos != payloadOff + payloadBytes)
                    {
                        e = "变体段总字节数不等于 payload";
                        ok = false;
                    }
                }
            }
        }
        if (ok && tpl.series.enabled && payloadSeen && !variantHit &&
            tpl.series.fallback == "raw")
        {
            RtSeg rt;
            for (const SegSpec &seg : tpl.segments)
                if (seg.role == "payload")
                {
                    rt.spec = &seg;
                    break;
                }
            rt.offset = payloadOff;
            rt.bytes = payloadBytes;
            rt.count = payloadCount;
            const uint8_t *p = data + off + payloadOff;
            for (uint32_t i = 0; i < payloadBytes; ++i)
                rt.raw.push_back(p[i]);
            fields.push_back(std::move(rt));
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        std::sort(fields.begin(), fields.end(), [](const RtSeg &a, const RtSeg &b) { return a.offset < b.offset; });
        byName.clear();
        for (RtSeg &rt : fields)
            byName[rt.spec->name] = &rt;

        if (!checkFrame(fields, byName, pseudo, order, data + off, rawCtx, e))
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        /* counts 反算 */
        for (RtSeg &rt : fields)
        {
            const SegSpec &seg = *rt.spec;
            if (!seg.counts.enabled || seg.role != "length")
                continue;
            double declared = rt.raw.empty() ? 0 : rt.raw[0];
            double actual = 0;
            if (seg.counts.expr)
            {
                if (!evalExpr(seg.counts.expr, rawCtx, actual, e))
                {
                    ok = false;
                    break;
                }
            }
            else
            {
                for (const std::string &fn : seg.counts.fields)
                {
                    auto pit = pseudo.find(fn);
                    if (pit != pseudo.end())
                    {
                        actual += (seg.counts.unit == "bytes")
                                      ? pit->second.second
                                      : (payloadCount ? payloadCount : pit->second.second);
                        continue;
                    }
                    auto it = byName.find(fn);
                    if (it == byName.end())
                    {
                        e = "counts 引用未知段: " + fn;
                        ok = false;
                        break;
                    }
                    actual += (seg.counts.unit == "bytes") ? it->second->bytes : it->second->count;
                }
            }
            if (ok && declared != actual)
            {
                e = "长度段 " + seg.name + " 与 counts 不一致";
                ok = false;
            }
            if (!ok)
                break;
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            continue;
        }

        double ts = frameIndex;
        if (tpl.hasTimestamp)
        {
            double v = 0;
            if (!evalExpr(tpl.timestampExpr, tpl.timestampRaw ? rawCtx : physCtx, v, e))
            {
                if (tpl.timestampFallback == "index")
                    v = frameIndex;
                else
                {
                    ++off;
                    ++rejected;
                    continue;
                }
            }
            if (tpl.timestampUnit == "ms")
                v *= 0.001;
            else if (tpl.timestampUnit == "us")
                v *= 0.000001;
            if (tpl.timestampOrigin == "first_frame")
            {
                if (!haveBase)
                {
                    baseTs = v;
                    haveBase = true;
                }
                ts = v - baseTs;
            }
            else
            {
                ts = v;
            }
        }

        bool anyChannel = false;
        for (RtSeg &rt : fields)
        {
            const SegSpec &seg = *rt.spec;
            const bool isPayload = seg.role == "payload";
            const bool skipRole = seg.role == "sync" || seg.role == "length" || seg.role == "checksum";
            if (seg.type.type == FType::STR)
                continue;
            if (seg.hidden || !seg.plot || skipRole || seg.hasValue)
                continue;
            if (isPayload && tpl.series.enabled && variantHit)
                continue;
            if (rt.raw.size() > 256)
                ++warnings;
            const uint32_t limit = std::min<uint32_t>(rt.count, 256);
            const std::string prefix = "frame." + (seg.fromVariant ? seg.prefix : std::string());
            if (rt.count == 1)
            {
                PsChannelData &ch = chans[prefix + seg.name];
                ch.name = prefix + seg.name;
                ch.unit = seg.unit;
                ch.t.push_back(ts);
                ch.v.push_back(rt.phys.empty() ? rt.raw[0] : rt.phys[0]);
                anyChannel = true;
            }
            else
            {
                for (uint32_t i = 0; i < limit; ++i)
                {
                    const std::string name = prefix + seg.name + "[" + std::to_string(i) + "]";
                    PsChannelData &ch = chans[name];
                    ch.name = name;
                    ch.unit = seg.unit;
                    ch.t.push_back(ts);
                    ch.v.push_back(rt.phys.empty() ? rt.raw[i] : rt.phys[i]);
                    anyChannel = true;
                }
            }
        }
        if (!anyChannel)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            continue;
        }

        off += pos;
        frameIndex += 1;
    }

    if (chans.empty())
    {
        std::fprintf(stderr, "PlotScope frame: no channels, off=%zu rejected=%llu last=%s\n",
                     off, static_cast<unsigned long long>(rejected), e.c_str());
        psWriteErr(err, errCap, "顺序协议没有可绘制通道；检查 role/plot/hidden 或协议内容");
        return 3;
    }

    std::vector<PsChannelData> out;
    for (auto &kv : chans)
        out.push_back(std::move(kv.second));
    std::string buildErr;
    PsDataset *ds = psBuildDataset(out, buildErr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, "构建 dataset 失败: " + buildErr);
        return 4;
    }
    *dataset = ds;
    if (warnings > 0)
    {
        /* 告警走 stderr，不污染错误返回 */
        std::fprintf(stderr, "PlotScope frame: %llu 个数组通道超过 256，仅绘制前 256\n",
                     static_cast<unsigned long long>(warnings));
    }
    return 0;
}
