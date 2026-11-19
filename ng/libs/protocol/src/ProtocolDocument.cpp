#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace nextpilot::plotscope::protocol
{

/* ---------------- 表达式解析 ---------------- */

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
            const int prec = precedence(op);
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
            const size_t start = m_pos;
            const bool hex = (m_s[m_pos] == '0' && m_pos + 1 < m_s.size() &&
                              (m_s[m_pos + 1] == 'x' || m_s[m_pos + 1] == 'X'));
            if (hex)
            {
                m_pos += 2;
                while (m_pos < m_s.size() && std::isxdigit(static_cast<unsigned char>(m_s[m_pos])))
                    ++m_pos;
            }
            else
            {
                while (m_pos < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_pos])))
                    ++m_pos;
                // 小数/指数：切到浮点字面量
                bool isFloat = false;
                if (m_pos < m_s.size() && m_s[m_pos] == '.')
                {
                    isFloat = true;
                    ++m_pos;
                    while (m_pos < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_pos])))
                        ++m_pos;
                }
                if (m_pos < m_s.size() && (m_s[m_pos] == 'e' || m_s[m_pos] == 'E'))
                {
                    isFloat = true;
                    ++m_pos;
                    if (m_pos < m_s.size() && (m_s[m_pos] == '+' || m_s[m_pos] == '-'))
                        ++m_pos;
                    while (m_pos < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_pos])))
                        ++m_pos;
                }
                const std::string tok = m_s.substr(start, m_pos - start);
                ExprPtr n = std::make_shared<Expr>();
                n->kind = Expr::Num;
                if (isFloat)
                {
                    n->isInt = false;
                    n->num = std::strtod(tok.c_str(), nullptr);
                }
                else
                {
                    n->isInt = true;
                    n->ival = static_cast<int64_t>(std::strtoull(tok.c_str(), nullptr, 10));
                }
                return n;
            }
            const std::string tok = m_s.substr(start, m_pos - start);
            ExprPtr n = std::make_shared<Expr>();
            n->kind = Expr::Num;
            n->isInt = true;
            n->ival = static_cast<int64_t>(std::strtoull(tok.c_str(), nullptr, 16));
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

/* ---------------- 类型化求值 ---------------- */

namespace
{

bool isText(const TypedValue &v) { return v.type == ValueType::Text; }

// 两个数值是不是"整型族"。Text 不算。
bool compareNumeric(const TypedValue &a, const TypedValue &b, int &cmp, std::string &err)
{
    if (isText(a) || isText(b))
    {
        err = "文本值不能参与数值比较";
        return false;
    }
    const bool af = isFloatType(a.type), bf = isFloatType(b.type);
    if (af != bf)
    {
        err = "整型与浮点不能直接比较，需要显式 toF64/toI64";
        return false;
    }
    if (af)
    {
        const double x = a.f, y = b.f;
        cmp = (x < y) ? -1 : (x > y ? 1 : 0);
        return true;
    }
    if (a.isSigned() || b.isSigned())
    {
        const int64_t x = a.asI64(), y = b.asI64();
        cmp = (x < y) ? -1 : (x > y ? 1 : 0);
        return true;
    }
    const uint64_t x = a.asU64(), y = b.asU64();
    cmp = (x < y) ? -1 : (x > y ? 1 : 0);
    return true;
}

bool intResult(int64_t v, bool asUnsigned, TypedValue &out)
{
    if (asUnsigned)
    {
        out = TypedValue::fromU(static_cast<uint64_t>(v), ValueType::U64);
        return true;
    }
    out = TypedValue::fromI(v, ValueType::I64);
    return true;
}

} // namespace

bool EvalCtx::get(const std::string &n, int index, TypedValue &out, std::string &err) const
{
    auto it = values.find(n);
    if (it == values.end())
    {
        err = "表达式引用未知字段: " + n;
        return false;
    }
    const TypedVec &col = it->second;
    if (index < 0)
    {
        if (col.size() != 1)
        {
            err = "字段 " + n + " 是数组，需要下标";
            return false;
        }
        out = col.at(0);
        return true;
    }
    if (index >= static_cast<int>(col.size()))
    {
        err = "字段 " + n + " 下标越界";
        return false;
    }
    out = col.at(static_cast<size_t>(index));
    return true;
}

bool evalExpr(const ExprPtr &e, const EvalCtx &ctx, TypedValue &out, std::string &err)
{
    if (!e)
    {
        err = "空表达式";
        return false;
    }
    switch (e->kind)
    {
    case Expr::Num:
        if (e->isInt)
            out = TypedValue::fromI(e->ival, ValueType::I64);
        else
            out = TypedValue::fromF(e->num, ValueType::F64);
        return true;
    case Expr::Ref:
    {
        int idx = -1;
        if (e->b)
        {
            int64_t iv = 0;
            if (!evalExprInt(e->b, ctx, iv, err))
                return false;
            idx = static_cast<int>(iv);
        }
        return ctx.get(e->name, idx, out, err);
    }
    case Expr::Un:
    {
        TypedValue v;
        if (!evalExpr(e->a, ctx, v, err))
            return false;
        if (isText(v))
        {
            err = "文本值不能取负";
            return false;
        }
        if (isFloatType(v.type))
        {
            out = TypedValue::fromF(-v.f, ValueType::F64);
            return true;
        }
        out = TypedValue::fromI(-v.asI64(), ValueType::I64);
        return true;
    }
    case Expr::Fn:
    {
        if (e->fn == "toF64" || e->fn == "toI64" || e->fn == "toU64")
        {
            TypedValue a;
            if (!e->a || !evalExpr(e->a, ctx, a, err))
                return false;
            if (isText(a))
            {
                err = e->fn + " 不接受文本值";
                return false;
            }
            if (e->fn == "toF64")
                out = TypedValue::fromF(a.asDouble(), ValueType::F64);
            else if (e->fn == "toI64")
                out = TypedValue::fromI(a.asI64(), ValueType::I64);
            else
                out = TypedValue::fromU(a.asU64(), ValueType::U64);
            return true;
        }
        TypedValue a;
        if (!e->a || !evalExpr(e->a, ctx, a, err))
            return false;
        if (e->fn == "abs")
        {
            if (isText(a))
            {
                err = "abs 不接受文本值";
                return false;
            }
            if (isFloatType(a.type))
                out = TypedValue::fromF(std::fabs(a.f), ValueType::F64);
            else
                out = TypedValue::fromI(a.asI64() < 0 ? -a.asI64() : a.asI64(), ValueType::I64);
            return true;
        }
        TypedValue b;
        if (!e->b)
        {
            err = "函数参数不足: " + e->fn;
            return false;
        }
        if (!evalExpr(e->b, ctx, b, err))
            return false;
        if (e->fn == "min" || e->fn == "max")
        {
            int cmp = 0;
            if (!compareNumeric(a, b, cmp, err))
                return false;
            const bool takeA = (e->fn == "min") ? (cmp <= 0) : (cmp >= 0);
            out = takeA ? a : b;
            return true;
        }
        if (e->fn == "clamp")
        {
            TypedValue c;
            if (!e->c || !evalExpr(e->c, ctx, c, err))
            {
                err = "clamp 需要 3 个参数";
                return false;
            }
            int ab = 0, ac = 0;
            if (!compareNumeric(a, b, ab, err) || !compareNumeric(a, c, ac, err))
                return false;
            out = (ab < 0) ? b : (ac > 0 ? c : a);
            return true;
        }
        err = "未知函数: " + e->fn;
        return false;
    }
    case Expr::Bin:
    {
        TypedValue a, b;
        if (!evalExpr(e->a, ctx, a, err) || !evalExpr(e->b, ctx, b, err))
            return false;
        if (isText(a) || isText(b))
        {
            err = "文本值不能参与运算";
            return false;
        }
        const bool af = isFloatType(a.type), bf = isFloatType(b.type);
        if (af != bf)
        {
            err = "整型与浮点不能直接混算，需要显式 toF64/toI64";
            return false;
        }
        if (af)
        {
            const double x = a.f, y = b.f;
            switch (e->op)
            {
            case '+': out = TypedValue::fromF(x + y, ValueType::F64); return true;
            case '-': out = TypedValue::fromF(x - y, ValueType::F64); return true;
            case '*': out = TypedValue::fromF(x * y, ValueType::F64); return true;
            case '/':
                if (y == 0.0) { err = "表达式除零"; return false; }
                out = TypedValue::fromF(x / y, ValueType::F64);
                return true;
            default:
                err = "浮点值不支持位运算/取模";
                return false;
            }
        }

        const bool anySigned = a.isSigned() || b.isSigned();
        if (anySigned)
        {
            const int64_t x = a.asI64(), y = b.asI64();
            switch (e->op)
            {
            case '+': return intResult(x + y, false, out);
            case '-': return intResult(x - y, false, out);
            case '*': return intResult(x * y, false, out);
            case '/':
                if (y == 0) { err = "表达式除零"; return false; }
                return intResult(x / y, false, out);
            case '%':
                if (y == 0) { err = "表达式取模零"; return false; }
                return intResult(x % y, false, out);
            case '&': return intResult(x & y, false, out);
            case '|': return intResult(x | y, false, out);
            case '^': return intResult(x ^ y, false, out);
            case 100: return intResult(x << y, false, out);
            case 101: return intResult(x >> y, false, out);
            default: err = "未知运算符"; return false;
            }
        }
        const uint64_t x = a.asU64(), y = b.asU64();
        switch (e->op)
        {
        case '+': return intResult(static_cast<int64_t>(x + y), true, out);
        case '-': return intResult(static_cast<int64_t>(x - y), true, out);
        case '*': return intResult(static_cast<int64_t>(x * y), true, out);
        case '/':
            if (y == 0) { err = "表达式除零"; return false; }
            return intResult(static_cast<int64_t>(x / y), true, out);
        case '%':
            if (y == 0) { err = "表达式取模零"; return false; }
            return intResult(static_cast<int64_t>(x % y), true, out);
        case '&': return intResult(static_cast<int64_t>(x & y), true, out);
        case '|': return intResult(static_cast<int64_t>(x | y), true, out);
        case '^': return intResult(static_cast<int64_t>(x ^ y), true, out);
        case 100: return intResult(static_cast<int64_t>(x << y), true, out);
        case 101: return intResult(static_cast<int64_t>(x >> y), true, out);
        default: err = "未知运算符"; return false;
        }
    }
    }
    err = "表达式求值失败";
    return false;
}

bool evalExprInt(const ExprPtr &e, const EvalCtx &ctx, int64_t &out, std::string &err)
{
    TypedValue v;
    if (!evalExpr(e, ctx, v, err))
        return false;
    if (isText(v))
    {
        err = "此处需要数值，得到文本";
        return false;
    }
    if (isFloatType(v.type))
    {
        err = "此处需要整型，得到浮点（需要显式 toI64）";
        return false;
    }
    out = v.asI64();
    return true;
}

bool evalExprDouble(const ExprPtr &e, const EvalCtx &ctx, double &out, std::string &err)
{
    TypedValue v;
    if (!evalExpr(e, ctx, v, err))
        return false;
    if (isText(v))
    {
        err = "此处需要数值，得到文本";
        return false;
    }
    out = v.asDouble();
    return true;
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
        const double d = v.toDouble();
        ExprPtr n = std::make_shared<Expr>();
        n->kind = Expr::Num;
        if (d == std::trunc(d) && std::fabs(d) < 9.0e15)
        {
            n->isInt = true;
            n->ival = static_cast<int64_t>(d);
        }
        else
        {
            n->isInt = false;
            n->num = d;
        }
        return n;
    }
    if (v.isString())
        return parseExprText(v.toString().toStdString(), err);
    err = "表达式必须是数字或字符串";
    return nullptr;
}

/* ---------------- JSON 读取 ---------------- */

static std::string numberKey(double v)
{
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return "n:" + os.str();
}

static std::string stringKey(const std::string &v) { return "s:" + v; }

// 安全解析 u32：支持 0x/0X 十六进制前缀；非法字符、进制不符、越界均写 err 返回 false，
// 不抛 std::invalid_argument / std::out_of_range（旧 stoul 会直接终止进程）。
static bool parseU32Safe(const std::string &text, uint32_t &out, std::string &err)
{
    std::string body = text;
    int base = 10;
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
    {
        base = 16;
        body = body.substr(2);
    }
    if (body.empty())
    {
        err = "数值为空: " + text;
        return false;
    }
    unsigned long long v = 0;
    for (char ch : body)
    {
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else
        {
            err = "非法数值字符: " + text;
            return false;
        }
        if (d >= base)
        {
            err = "数值与进制不符: " + text;
            return false;
        }
        v = v * static_cast<unsigned>(base) + d;
        if (v > 0xFFFFFFFFull)
        {
            err = "数值越界(>u32): " + text;
            return false;
        }
    }
    out = static_cast<uint32_t>(v);
    return true;
}

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
            c.rhs = jsonExpr(o.value(QStringLiteral("value")), err);
        else if (o.contains(QStringLiteral("expr")))
            c.rhs = jsonExpr(o.value(QStringLiteral("expr")), err);
        if (o.contains(QStringLiteral("value")) || o.contains(QStringLiteral("expr")))
        {
            if (!c.rhs)
                return false;
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
            {
                if (iv.isString())
                    c.values.push_back(TypedValue::fromText(iv.toString().toStdString()));
                else
                    c.values.push_back(TypedValue::fromI(static_cast<int64_t>(iv.toDouble()),
                                                         ValueType::I64));
            }
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
            {
                if (!parseU32Safe(o.value(QStringLiteral("poly")).toString().toStdString(), c.poly, err))
                    return false;
            }
            if (o.contains(QStringLiteral("init")))
            {
                if (!parseU32Safe(o.value(QStringLiteral("init")).toString().toStdString(), c.init, err))
                    return false;
            }
            if (o.contains(QStringLiteral("xorout")))
            {
                if (!parseU32Safe(o.value(QStringLiteral("xorout")).toString().toStdString(), c.xorout, err))
                    return false;
            }
            c.refin = o.value(QStringLiteral("refin")).toBool(c.refin);
            c.refout = o.value(QStringLiteral("refout")).toBool(c.refout);
            for (const QJsonValue &ov : o.value(QStringLiteral("over")).toArray())
                c.over.push_back(ov.toString().toStdString());
        }
        const bool sequenceOp = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" ||
                                 c.op == "sum8" || c.op == "sum16" || c.op == "xor8" ||
                                 c.op == "parity" || c.op == "in");
        if (!sequenceOp && c.rhs == nullptr)
        {
            err = "check 缺少比较值";
            return false;
        }
        out.push_back(c);
    }
    return true;
}

static bool parseEmit(const QString &s, EmitMode &out, std::string &err)
{
    if (s.isEmpty() || s == QLatin1String("channel")) { out = EmitMode::Channel; return true; }
    if (s == QLatin1String("aux")) { out = EmitMode::Aux; return true; }
    if (s == QLatin1String("none")) { out = EmitMode::None; return true; }
    err = "emit 只能是 channel/aux/none: " + s.toStdString();
    return false;
}

bool parseAliases(const QJsonObject &root, std::map<std::string, AliasDef> &out, std::string &err)
{
    const QJsonObject to = root.value(QStringLiteral("types")).toObject();
    for (auto it = to.begin(); it != to.end(); ++it)
    {
        const QJsonObject ao = it.value().toObject();
        AliasDef ad;
        if (!parseType(ao.value(QStringLiteral("type")).toString(QStringLiteral("u8")).toStdString(),
                       ad.ti, err))
            return false;
        ad.scale = ao.value(QStringLiteral("scale")).toDouble(1.0);
        ad.offset = ao.value(QStringLiteral("offset")).toDouble(0.0);
        ad.unit = ao.value(QStringLiteral("unit")).toString().toStdString();
        if (ao.contains(QStringLiteral("range")))
        {
            const QJsonValue rv = ao.value(QStringLiteral("range"));
            ad.hasRange = true;
            if (rv.isArray())
            {
                const QJsonArray ra = rv.toArray();
                ad.rangeMin = ra.at(0).toDouble(0);
                ad.rangeMax = ra.size() > 1 ? ra.at(1).toDouble(0) : ad.rangeMin;
            }
            else
            {
                const QJsonObject ro = rv.toObject();
                ad.rangeMin = ro.value(QStringLiteral("min")).toDouble(0);
                ad.rangeMax = ro.value(QStringLiteral("max")).toDouble(0);
            }
        }
        out[it.key().toStdString()] = ad;
    }
    return true;
}

bool parseSegment(const QJsonObject &o, const std::string &defaultEndian, SegSpec &seg, std::string &err,
                  bool docAscii, const std::map<std::string, AliasDef> *aliases = nullptr)
{
    seg.name = o.value(QStringLiteral("name")).toString().toStdString();
    if (seg.name.empty())
    {
        err = "segment 缺少 name";
        return false;
    }
    // ---- ASCII 词法字段（SPEC §7）：先解析，因为 ASCII 段的 count 可缺省（默认 1） ----
    seg.literal = o.value(QStringLiteral("literal")).toString().toStdString();
    seg.key = o.value(QStringLiteral("key")).toString().toStdString();
    if (o.contains(QStringLiteral("delimiter")))
    {
        seg.hasDelimiter = true;
        const QJsonValue dv = o.value(QStringLiteral("delimiter"));
        if (dv.isArray())
        {
            for (const QJsonValue &v : dv.toArray())
                seg.delimiter.push_back(v.toString().toStdString());
        }
        else
            seg.delimiter.push_back(dv.toString().toStdString());
    }
    seg.repeater = o.value(QStringLiteral("repeater")).toString().toStdString();
    if (o.contains(QStringLiteral("width")))
    {
        seg.hasWidth = true;
        seg.width = static_cast<uint32_t>(o.value(QStringLiteral("width")).toInt(0));
    }
    seg.isRest = o.value(QStringLiteral("rest")).toBool(false);
    if (o.contains(QStringLiteral("repeat")))
    {
        const QJsonValue rv = o.value(QStringLiteral("repeat"));
        if (rv.isString())
            seg.repeatMode = rv.toString().toStdString();
        else
        {
            const QJsonObject ro = rv.toObject();
            seg.repeatMode = ro.value(QStringLiteral("mode")).toString().toStdString();
            const QString until = ro.value(QStringLiteral("until")).toString();
            if (!until.isEmpty())
            {
                seg.repeatMode = "until";
                seg.repeatUntil = parseExprText(until.toStdString(), err);
                if (!seg.repeatUntil)
                    return false;
            }
        }
    }
    // SPEC §2.1：repeat:"until" + 顶层 until 键两种写法等价
    if (seg.repeatMode == "until" && !seg.repeatUntil && o.contains(QStringLiteral("until")))
    {
        seg.repeatUntil = parseExprText(o.value(QStringLiteral("until")).toVariant().toString().toStdString(), err);
        if (!seg.repeatUntil)
            return false;
    }
    if (o.contains(QStringLiteral("range")))
    {
        const QJsonValue rv = o.value(QStringLiteral("range"));
        seg.hasRange = true;
        if (rv.isArray())
        {
            const QJsonArray ra = rv.toArray();
            seg.rangeMin = ra.at(0).toDouble(0);
            seg.rangeMax = ra.size() > 1 ? ra.at(1).toDouble(0) : seg.rangeMin;
        }
        else
        {
            const QJsonObject ro = rv.toObject();
            seg.rangeMin = ro.value(QStringLiteral("min")).toDouble(0);
            seg.rangeMax = ro.value(QStringLiteral("max")).toDouble(0);
        }
    }
    if (o.contains(QStringLiteral("enum")))
    {
        seg.hasEnum = true;
        for (const QJsonValue &ev : o.value(QStringLiteral("enum")).toArray())
            seg.enumValues.push_back(ev.toDouble(0));
    }
    seg.base = static_cast<int64_t>(o.value(QStringLiteral("base")).toDouble(10));
    if (o.contains(QStringLiteral("fixed")))
    {
        seg.hasFixedDigits = true;
        seg.fixedDigits = static_cast<uint32_t>(o.value(QStringLiteral("fixed")).toInt(0));
    }
    seg.signedFlag = o.value(QStringLiteral("signed")).toBool(false);
    seg.sci = o.value(QStringLiteral("sci")).toBool(false);
    seg.as = o.value(QStringLiteral("as")).toString().toStdString();
    seg.emptyPolicy = o.value(QStringLiteral("empty")).toString().toStdString();
    seg.pattern = o.value(QStringLiteral("pattern")).toString().toStdString();
    seg.onMismatch = o.value(QStringLiteral("on_mismatch")).toString().toStdString();
    if (o.contains(QStringLiteral("transform")))
    {
        const QJsonValue tv = o.value(QStringLiteral("transform"));
        if (tv.isArray())
        {
            for (const QJsonValue &v : tv.toArray())
            {
                TransformStep st;
                if (v.isString())
                    st.op = v.toString().toStdString();
                else
                {
                    const QJsonObject so = v.toObject();
                    st.op = so.value(QStringLiteral("op")).toString().toStdString();
                    st.split = so.value(QStringLiteral("split")).toInt(2);
                    st.k = so.value(QStringLiteral("k")).toDouble(1.0);
                    st.b = so.value(QStringLiteral("b")).toDouble(0.0);
                    st.hasMin = so.contains(QStringLiteral("min"));
                    st.hasMax = so.contains(QStringLiteral("max"));
                    st.minVal = so.value(QStringLiteral("min")).toDouble(0.0);
                    st.maxVal = so.value(QStringLiteral("max")).toDouble(0.0);
                }
                if (!st.op.empty())
                    seg.transform.push_back(st);
            }
        }
    }
    if (o.contains(QStringLiteral("digest")))
    {
        const QJsonValue dv = o.value(QStringLiteral("digest"));
        if (!dv.isObject())
        {
            err = "segment " + seg.name + " 的 digest 必须是对象";
            return false;
        }
        const QJsonObject dgo = dv.toObject();
        seg.digest.enabled = true;
        seg.digest.alg = dgo.value(QStringLiteral("alg")).toString().toStdString();
        seg.digest.script = dgo.value(QStringLiteral("code")).toString().toStdString();
        seg.digest.from = dgo.value(QStringLiteral("from")).toString().toStdString();
        seg.digest.to = dgo.value(QStringLiteral("to")).toString().toStdString();
        if (dgo.contains(QStringLiteral("inject")))
        {
            seg.digest.hasInject = true;
            seg.digest.inject =
                static_cast<uint32_t>(dgo.value(QStringLiteral("inject")).toDouble(0));
        }
    }

    const bool asciiHints = docAscii || !seg.literal.empty() || seg.hasDelimiter || seg.hasWidth ||
                            seg.isRest || !seg.repeater.empty();
    if (!o.contains(QStringLiteral("count")))
    {
        if (!asciiHints)
        {
            err = "segment " + seg.name + " 缺少 count";
            return false;
        }
        auto one = std::make_shared<Expr>();
        one->kind = Expr::Num;
        one->isInt = true;
        one->ival = 1;
        seg.countExpr = one;
    }
    else
    {
        seg.countExpr = jsonExpr(o.value(QStringLiteral("count")), err);
    }
    if (!seg.countExpr)
        return false;
    {
        const std::string typeName =
            o.value(QStringLiteral("type")).toString(QStringLiteral("u8")).toStdString();
        const AliasDef *alias = nullptr;
        if (aliases)
        {
            auto it = aliases->find(typeName);
            if (it != aliases->end())
                alias = &it->second;
        }
        if (alias)
        {
            seg.type = alias->ti;
            if (seg.scale == 1.0 && alias->scale != 1.0)
                seg.scale = alias->scale;
            if (seg.offset == 0.0 && alias->offset != 0.0)
                seg.offset = alias->offset;
            if (seg.unit.empty())
                seg.unit = alias->unit;
            if (!seg.hasRange && alias->hasRange)
            {
                seg.hasRange = true;
                seg.rangeMin = alias->rangeMin;
                seg.rangeMax = alias->rangeMax;
            }
        }
        else if (!parseType(typeName, seg.type, err))
            return false;
    }
    // 数值解释覆盖类型：base 非 10 或声明了小数位数，说明它是整数记法，不该退化成 f64。
    if (seg.as.empty() && seg.type.type == ValueType::F64 && (seg.base != 10 || seg.hasFixedDigits))
        seg.type = {ValueType::I64, 8};

    const std::string endian = o.value(QStringLiteral("endian")).toString(QString::fromStdString(defaultEndian)).toStdString();
    seg.bigEndian = (endian == "big" || endian == "be");
    seg.scale = o.value(QStringLiteral("scale")).toDouble(1.0);
    seg.offset = o.value(QStringLiteral("offset")).toDouble(0.0);
    seg.unit = o.value(QStringLiteral("unit")).toString().toStdString();
    seg.description = o.value(QStringLiteral("description")).toString().toStdString();

    if (!parseEmit(o.value(QStringLiteral("emit")).toString(), seg.emitMode, err))
        return false;

    if (o.contains(QStringLiteral("bit")))
    {
        const QJsonValue bv = o.value(QStringLiteral("bit"));
        if (!bv.isObject())
        {
            err = "segment " + seg.name + " 的 bit 必须是对象";
            return false;
        }
        const QJsonObject bo = bv.toObject();
        seg.bit.enabled = true;
        seg.bit.base = bo.value(QStringLiteral("base")).toString().toStdString();
        seg.bit.offset = static_cast<int64_t>(bo.value(QStringLiteral("offset")).toDouble(0));
        seg.bit.width = bo.value(QStringLiteral("width")).toInt(0);
        seg.bit.order = bo.value(QStringLiteral("order")).toString(QStringLiteral("lsb0")).toStdString();
    }
    if (o.contains(QStringLiteral("if")))
    {
        seg.cond = jsonExpr(o.value(QStringLiteral("if")), err);
        if (!seg.cond)
            return false;
    }
    if (o.contains(QStringLiteral("expect")))
    {
        seg.expectExpr = jsonExpr(o.value(QStringLiteral("expect")), err);
        if (!seg.expectExpr)
            return false;
    }
    if (o.contains(QStringLiteral("checks")) && o.value(QStringLiteral("checks")).isArray())
    {
        if (!parseChecks(o.value(QStringLiteral("checks")).toArray(), seg.checks, err))
            return false;
    }
    if (o.contains(QStringLiteral("length")))
    {
        const QJsonValue cv = o.value(QStringLiteral("length"));
        if (cv.isObject())
        {
            const QJsonObject co = cv.toObject();
            seg.length.enabled = true;
            for (const QJsonValue &fv : co.value(QStringLiteral("fields")).toArray())
                seg.length.fields.push_back(fv.toString().toStdString());
            seg.length.unit = co.value(QStringLiteral("unit")).toString(QStringLiteral("elements")).toStdString();
            if (co.contains(QStringLiteral("expr")))
            {
                seg.length.expr = jsonExpr(co.value(QStringLiteral("expr")), err);
                if (!seg.length.expr)
                    return false;
            }
        }
        else if (cv.isString())
        {
            seg.length.enabled = true;
            seg.length.expr = jsonExpr(cv, err);
            if (!seg.length.expr)
                return false;
        }
        else
        {
            err = "segment " + seg.name + " 的 length 必须是对象或表达式";
            return false;
        }
    }
    return true;
}

bool parseDocument(const QJsonObject &root, ProtocolDocument &t, std::string &err)
{
    t.version = root.value(QStringLiteral("version")).toString(QStringLiteral("1")).toStdString();
    t.description = root.value(QStringLiteral("description")).toString().toStdString();
    t.layout = root.value(QStringLiteral("layout")).toString(QStringLiteral("sequential")).toStdString();
    t.name = root.value(QStringLiteral("name")).toString(QStringLiteral("frame")).toStdString();
    t.endian = root.value(QStringLiteral("endian")).toString(QStringLiteral("little")).toStdString();
    {
        // 帧尾候选集合：声明了就硬校验，没声明就不管（SPEC §7.2）。
        const QJsonValue tv = root.contains(QStringLiteral("term"))
                                  ? root.value(QStringLiteral("term"))
                                  : root.value(QStringLiteral("terminator"));
        if (!tv.isUndefined() && !tv.isNull())
        {
            t.hasTerminator = true;
            if (tv.isArray())
            {
                for (const QJsonValue &v : tv.toArray())
                    t.terminator.push_back(v.toString().toStdString());
            }
            else
                t.terminator.push_back(tv.toString().toStdString());
        }
    }
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

    // 只要协议里有字符边界段，字符段的 count 即可缺省（默认 1）。
    // 这是段级事实，不是文档级模式。
    if (!parseAliases(root, t.aliases, err))
        return false;
    bool docAscii = false;
    for (const QJsonValue &sv : root.value(QStringLiteral("segments")).toArray())
    {
        const QJsonObject sg = sv.toObject();
        if (sg.contains(QStringLiteral("key")) || sg.contains(QStringLiteral("literal")) ||
            sg.contains(QStringLiteral("delimiter")) ||
            sg.contains(QStringLiteral("width")) || sg.contains(QStringLiteral("rest")) ||
            sg.contains(QStringLiteral("repeater")))
        {
            docAscii = true;
            break;
        }
    }

    for (const QJsonValue &sv : root.value(QStringLiteral("segments")).toArray())
    {
        SegSpec seg;
        if (!parseSegment(sv.toObject(), t.endian, seg, err, docAscii, &t.aliases))
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
                if (!parseSegment(fv.toObject(), t.endian, seg, err, docAscii, &t.aliases))
                    return false;
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

/* ---------------- 序列化 ---------------- */

static std::string numText(const Expr &e)
{
    if (e.isInt)
    {
        std::ostringstream os;
        os << e.ival;
        return os.str();
    }
    std::ostringstream os;
    os << std::setprecision(17) << e.num;
    return os.str();
}

static std::string numTextValue(const TypedValue &v)
{
    if (v.type == ValueType::Text)
        return v.s;
    if (isFloatType(v.type))
    {
        std::ostringstream os;
        os << std::setprecision(17) << v.f;
        return os.str();
    }
    if (v.isSigned())
    {
        std::ostringstream os;
        os << v.i;
        return os.str();
    }
    std::ostringstream os;
    os << v.u;
    return os.str();
}

std::string exprText(const ExprPtr &e)
{
    if (!e)
        return "0";
    switch (e->kind)
    {
    case Expr::Num:
        return numText(*e);
    case Expr::Ref:
        return e->b ? e->name + "[" + exprText(e->b) + "]" : e->name;
    case Expr::Un:
        return "-" + exprText(e->a);
    case Expr::Bin:
    {
        std::string op(1, static_cast<char>(e->op));
        return "(" + exprText(e->a) + " " + op + " " + exprText(e->b) + ")";
    }
    case Expr::Fn:
    {
        std::string s = e->fn + "(" + exprText(e->a);
        if (e->b)
            s += ", " + exprText(e->b);
        if (e->c)
            s += ", " + exprText(e->c);
        return s + ")";
    }
    }
    return "0";
}

static QJsonValue exprJson(const ExprPtr &e)
{
    if (!e)
        return QJsonValue();
    if (e->kind == Expr::Num)
        return e->isInt ? QJsonValue(static_cast<qlonglong>(e->ival)) : QJsonValue(e->num);
    return QJsonValue(QString::fromStdString(exprText(e)));
}

static QString typeName(const TypeInfo &ti)
{
    switch (ti.type)
    {
    case ValueType::U8: return "u8";
    case ValueType::I8: return "i8";
    case ValueType::U16: return "u16";
    case ValueType::I16: return "i16";
    case ValueType::U32: return "u32";
    case ValueType::I32: return "i32";
    case ValueType::U64: return "u64";
    case ValueType::I64: return "i64";
    case ValueType::F32: return "f32";
    case ValueType::F64: return "f64";
    case ValueType::Bool: return "bool";
    case ValueType::Bcd: return "bcd";
    case ValueType::Text: return "char";
    }
    return "u8";
}

static QJsonObject checkJson(const CheckSpec &c)
{
    QJsonObject o;
    o["op"] = QString::fromStdString(c.op);
    if (c.scope != "element")
        o["scope"] = QString::fromStdString(c.scope);
    const bool seq = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" || c.op == "sum8" ||
                      c.op == "sum16" || c.op == "xor8" || c.op == "parity");
    if (seq)
    {
        QJsonArray over;
        for (const auto &r : c.over)
            over.append(QString::fromStdString(r));
        o["over"] = over;
        o["poly"] = QStringLiteral("0x%1").arg(c.poly, 0, 16);
        o["init"] = QStringLiteral("0x%1").arg(c.init, 0, 16);
        if (c.xorout)
            o["xorout"] = QStringLiteral("0x%1").arg(c.xorout, 0, 16);
        if (c.refin)
            o["refin"] = true;
        if (c.refout)
            o["refout"] = true;
    }
    else if (c.op == "range")
    {
        o["min"] = exprJson(c.rhs);
        o["max"] = exprJson(c.rhs2);
    }
    else if (c.op == "mask")
    {
        o["and"] = exprJson(c.rhs);
        o["eq"] = exprJson(c.rhs2);
    }
    else if (c.op == "in")
    {
        QJsonArray values;
        for (const TypedValue &v : c.values)
            values.append(v.type == ValueType::Text ? QJsonValue(QString::fromStdString(v.s))
                                                   : QJsonValue(v.asDouble()));
        o["values"] = values;
    }
    else if (c.rhs)
    {
        o["value"] = exprJson(c.rhs);
    }
    return o;
}

static QJsonObject segmentJson(const SegmentDef &seg, const ProtocolDocument &doc)
{
    QJsonObject o;
    o["name"] = QString::fromStdString(seg.name);
    const bool asciiSeg = isAsciiSegment(seg);
    // ASCII 段的 count 缺省就是 1，不必回写，避免模板里多一行噪音。
    if (!asciiSeg || !seg.countExpr || !seg.countExpr->isInt || seg.countExpr->ival != 1)
        o["count"] = exprJson(seg.countExpr);
    o["type"] = typeName(seg.type);
    if (asciiSeg)
    {
        if (!seg.literal.empty())
            if (!seg.key.empty())
                o["key"] = QString::fromStdString(seg.key);
            o["literal"] = QString::fromStdString(seg.literal);
        if (seg.hasDelimiter)
        {
            if (seg.delimiter.size() == 1)
                o["delimiter"] = QString::fromStdString(seg.delimiter.front());
            else
            {
                QJsonArray d;
                for (const auto &s : seg.delimiter)
                    d.append(QString::fromStdString(s));
                o["delimiter"] = d;
            }
        }
        if (!seg.repeater.empty())
            o["repeater"] = QString::fromStdString(seg.repeater);
        if (seg.hasWidth)
            o["width"] = static_cast<int>(seg.width);
        if (seg.isRest)
            o["rest"] = true;
        if (seg.base != 10)
            o["base"] = static_cast<qint64>(seg.base);
        if (seg.hasFixedDigits)
            o["fixed"] = static_cast<int>(seg.fixedDigits);
        if (seg.signedFlag)
            o["signed"] = true;
        if (seg.sci)
            o["sci"] = true;
        if (!seg.as.empty())
            o["as"] = QString::fromStdString(seg.as);
        if (!seg.emptyPolicy.empty())
            o["empty"] = QString::fromStdString(seg.emptyPolicy);
        if (!seg.pattern.empty())
            o["pattern"] = QString::fromStdString(seg.pattern);
        if (!seg.onMismatch.empty())
            o["on_mismatch"] = QString::fromStdString(seg.onMismatch);
        if (!seg.transform.empty())
        {
            QJsonArray tr;
            for (const auto &st : seg.transform)
            {
                QJsonObject t;
                t["op"] = QString::fromStdString(st.op);
                if (st.split != 2)
                    t["split"] = st.split;
                tr.append(t);
            }
            o["transform"] = tr;
        }
        if (seg.digest.enabled)
        {
            QJsonObject dg;
            if (!seg.digest.alg.empty())
                dg["alg"] = QString::fromStdString(seg.digest.alg);
            if (!seg.digest.script.empty())
                dg["code"] = QString::fromStdString(seg.digest.script);
            if (!seg.digest.from.empty())
                dg["from"] = QString::fromStdString(seg.digest.from);
            if (!seg.digest.to.empty())
                dg["to"] = QString::fromStdString(seg.digest.to);
            o["digest"] = dg;
        }
    }
    const bool docBig = (doc.endian == "big" || doc.endian == "be");
    if (seg.bigEndian != docBig)
        o["endian"] = seg.bigEndian ? "big" : "little";
    if (seg.scale != 1.0)
        o["scale"] = seg.scale;
    if (seg.offset != 0.0)
        o["offset"] = seg.offset;
    if (!seg.unit.empty())
        o["unit"] = QString::fromStdString(seg.unit);
    if (!seg.description.empty())
        o["description"] = QString::fromStdString(seg.description);
    if (seg.emitMode == EmitMode::None)
        o["emit"] = "none";
    else if (seg.emitMode == EmitMode::Aux)
        o["emit"] = "aux";
    if (seg.bit.enabled)
    {
        QJsonObject b;
        if (!seg.bit.base.empty())
            b["base"] = QString::fromStdString(seg.bit.base);
        b["offset"] = static_cast<qint64>(seg.bit.offset);
        b["width"] = seg.bit.width;
        b["order"] = QString::fromStdString(seg.bit.order);
        o["bit"] = b;
    }
    if (seg.cond)
        o["if"] = exprJson(seg.cond);
    if (seg.expectExpr)
        o["expect"] = exprJson(seg.expectExpr);
    if (!seg.checks.empty())
    {
        QJsonArray checks;
        for (const auto &c : seg.checks)
            checks.append(checkJson(c));
        o["checks"] = checks;
    }
    if (seg.length.enabled)
    {
        QJsonObject c;
        QJsonArray fields;
        for (const auto &f : seg.length.fields)
            fields.append(QString::fromStdString(f));
        c["fields"] = fields;
        c["unit"] = QString::fromStdString(seg.length.unit);
        if (seg.length.expr)
            c["expr"] = exprJson(seg.length.expr);
        o["length"] = c;
    }
    return o;
}

QJsonObject toJson(const ProtocolDocument &doc)
{
    QJsonObject root;
    root["version"] = QString::fromStdString(doc.version);
    root["name"] = QString::fromStdString(doc.name);
    if (!doc.description.empty())
        root["description"] = QString::fromStdString(doc.description);
    root["layout"] = QString::fromStdString(doc.layout);
    root["endian"] = QString::fromStdString(doc.endian);
    root["maxsize"] = static_cast<int>(doc.maxsize);
    if (doc.hasTerminator)
    {
        if (doc.terminator.size() == 1)
            root["term"] = QString::fromStdString(doc.terminator.front());
        else
        {
            QJsonArray t;
            for (const auto &s : doc.terminator)
                t.append(QString::fromStdString(s));
            root["term"] = t;
        }
    }

    if (doc.hasTimestamp && doc.timestampExpr)
    {
        QJsonObject t;
        t["expr"] = exprJson(doc.timestampExpr);
        if (doc.timestampUnit != "s")
            t["unit"] = QString::fromStdString(doc.timestampUnit);
        if (doc.timestampOrigin != "first_frame")
            t["origin"] = QString::fromStdString(doc.timestampOrigin);
        if (doc.timestampRaw)
            t["raw"] = true;
        if (doc.timestampFallback != "reject")
            t["fallback"] = QString::fromStdString(doc.timestampFallback);
        root["timestamp"] = t;
    }

    QJsonArray segs;
    for (const auto &seg : doc.segments)
        segs.append(segmentJson(seg, doc));
    root["segments"] = segs;

    if (doc.series.enabled && doc.series.selector)
    {
        QJsonObject s;
        s["selector"] = exprJson(doc.series.selector);
        s["payload"] = QString::fromStdString(doc.series.payload);
        if (doc.series.fallback != "reject")
            s["fallback"] = QString::fromStdString(doc.series.fallback);
        QJsonArray variants;
        for (const auto &v : doc.series.variants)
        {
            QJsonObject vo;
            vo["name"] = QString::fromStdString(v.name);
            QJsonArray values;
            for (const auto &k : v.keys)
            {
                if (k.rfind("s:", 0) == 0)
                    values.append(QString::fromStdString(k.substr(2)));
                else if (k.rfind("n:", 0) == 0)
                    values.append(QString::fromStdString(k.substr(2)).toDouble());
            }
            if (values.size() == 1)
                vo["value"] = values.first();
            else if (!values.isEmpty())
                vo["values"] = values;
            QJsonArray vsegs;
            for (const auto &seg : v.segments)
                vsegs.append(segmentJson(seg, doc));
            vo["segments"] = vsegs;
            variants.append(vo);
        }
        s["variants"] = variants;
        root["series"] = s;
    }
    return root;
}

} // namespace nextpilot::plotscope::protocol
