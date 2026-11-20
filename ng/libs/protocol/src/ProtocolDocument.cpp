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

// v3：expect 数组先归一成内部 checks 的形状，再走 parseChecks。
// 归一规则：mask 的 mask/value → and/eq；over 字符串 → 单元素数组；labels 忽略（只用于显示）。
static bool parseExpectArray(const QJsonArray &arr, std::vector<CheckSpec> &out, std::string &err)
{
    QJsonArray norm;
    for (const QJsonValue &vv : arr)
    {
        const QJsonObject item = vv.toObject();
        const QString op = item.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("script"))
        {
            err = "expect:script 暂未支持";
            return false;
        }
        QJsonObject o = item;
        if (op == QLatin1String("mask"))
        {
            o.insert(QStringLiteral("and"), item.value(QStringLiteral("mask")));
            o.insert(QStringLiteral("eq"), item.value(QStringLiteral("value")));
        }
        const QJsonValue ov = item.value(QStringLiteral("over"));
        if (ov.isString())
        {
            QJsonArray one;
            one.append(ov.toString());
            o.insert(QStringLiteral("over"), one);
        }
        norm.append(o);
    }
    return parseChecks(norm, out, err);
}

// transform 单条表达式：形如 self / self*k / self+b / self*k+b 的直接折成 scale+offset，
// 走原来的零开销物理换算路径；其余形式编译成表达式逐帧求值。
static bool foldLinearExpr(const QString &text, double *k, double *b)
{
    static const QRegularExpression re(QStringLiteral(
        "^self\\s*(?:\\*\\s*([-+]?\\d+(?:\\.\\d+)?))?\\s*(?:([-+])\\s*(\\d+(?:\\.\\d+)?))?$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return false;
    *k = 1.0;
    *b = 0.0;
    if (!m.captured(1).isEmpty())
        *k = m.captured(1).toDouble();
    if (!m.captured(3).isEmpty())
    {
        const double v = m.captured(3).toDouble();
        *b = (m.captured(2) == QStringLiteral("-")) ? -v : v;
    }
    return true;
}

// count 的 "宽@偏移"（bit 类型专用）。
static bool splitBitSpan(const QString &s, int *w, int64_t *off)
{
    const QStringList parts = s.split(QLatin1Char('@'));
    if (parts.size() != 2)
        return false;
    bool okW = false;
    bool okO = false;
    const int width = parts.at(0).trimmed().toInt(&okW);
    const int offset = parts.at(1).trimmed().toInt(&okO);
    if (!okW || !okO)
        return false;
    *w = width;
    *off = static_cast<int64_t>(offset);
    return true;
}

bool parseSegment(const QJsonObject &o, const std::string &defaultEndian, SegSpec &seg, std::string &err,
                  bool docText, const std::map<std::string, AliasDef> *aliases = nullptr)
{
    seg.name = o.value(QStringLiteral("name")).toString().toStdString();
    if (seg.name.empty())
    {
        err = "segment 缺少 name";
        return false;
    }

    // ---- text 词法（SPEC §10）：format 是新口径；老键仍收，供未迁完的模板过渡 ----
    seg.format = o.value(QStringLiteral("format")).toString().toStdString();
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
    seg.base = static_cast<int64_t>(o.value(QStringLiteral("base")).toDouble(10));
    if (o.contains(QStringLiteral("fixed")))
    {
        seg.hasFixedDigits = true;
        seg.fixedDigits = static_cast<uint32_t>(o.value(QStringLiteral("fixed")).toInt(0));
    }
    seg.signedFlag = o.value(QStringLiteral("signed")).toBool(false);
    seg.sci = o.value(QStringLiteral("sci")).toBool(false);
    seg.emptyPolicy = o.value(QStringLiteral("empty")).toString().toStdString();
    seg.pattern = o.value(QStringLiteral("pattern")).toString().toStdString();
    seg.onMismatch = o.value(QStringLiteral("on_mismatch")).toString().toStdString();

    // ---- count：数字 / 变量或表达式 / "*" ----
    const QJsonValue cv = o.value(QStringLiteral("count"));
    if (cv.isUndefined())
    {
        auto one = std::make_shared<Expr>();
        one->kind = Expr::Num;
        one->isInt = true;
        one->ival = 1;
        seg.countExpr = one;
    }
    else if (cv.isString() && cv.toString() == QLatin1String("*"))
    {
        seg.countWildcard = true;
        seg.repeatMode = "eos"; // 复用旧的"读到帧尾"路径，解码侧不必新增分支
        auto one = std::make_shared<Expr>();
        one->kind = Expr::Num;
        one->isInt = true;
        one->ival = 1;
        seg.countExpr = one;
    }
    else
    {
        seg.countExpr = jsonExpr(cv, err);
    }
    if (!seg.countExpr)
        return false;

    // ---- type：bit/rbit / blocks 名 / types 别名 / 内置类型 ----
    // rbit 与 bit 只差一个位序取值：bit = lsb0（字节内从低位起数），rbit = msb0（从高位起数）。
    // 位偏移的编号方式两者完全一致，所以不额外占键。
    const QString tn = o.value(QStringLiteral("type")).toString(QStringLiteral("u8")).toLower();
    const bool isRbit = (tn == QLatin1String("rbit"));
    if (tn == QLatin1String("bit") || isRbit)
    {
        int w = 0;
        int64_t off = 0;
        if (!cv.isString() || !splitBitSpan(cv.toString(), &w, &off))
        {
            err = "segment " + seg.name + " 是 bit 类型，count 必须写成 \"宽@偏移\"";
            return false;
        }
        seg.bit.enabled = true;
        seg.bit.base.clear();
        seg.bit.offset = off;
        seg.bit.width = w;
        seg.bit.order = isRbit ? "msb0" : "lsb0";
        seg.type = {ValueType::U64, 8, false};
    }
    else
    {
        const AliasDef *alias = nullptr;
        if (aliases)
        {
            auto it = aliases->find(tn.toStdString());
            if (it != aliases->end())
                alias = &it->second;
        }
        if (alias)
        {
            seg.type = alias->ti;
            if (seg.unit.empty())
                seg.unit = alias->unit;
        }
        else if (parseType(tn.toStdString(), seg.type, err))
        {
        }
        else if (o.value(QStringLiteral("type")).isString() &&
                 QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"))
                         .match(tn)
                         .hasMatch())
        {
            // 块名：先记下来，编译期展开（顺序块/位域块）
            seg.blockRef = tn.toStdString();
            seg.type = {ValueType::U64, 8, false};
        }
        else
            return false;
    }

    // 段级 endian 已删：只跟顶层走
    seg.bigEndian = (defaultEndian == "big" || defaultEndian == "be");
    seg.unit = o.value(QStringLiteral("unit")).toString().toStdString();
    seg.description = o.value(QStringLiteral("description")).toString().toStdString();

    {
        const QString out = o.value(QStringLiteral("output")).toString(QStringLiteral("channel"));
        if (!parseEmit(out, seg.emitMode, err))
            return false;
    }

    // ---- transform：表达式优先，折不了才留表达式 ----
    if (o.contains(QStringLiteral("transform")))
    {
        const QJsonValue tv = o.value(QStringLiteral("transform"));
        if (tv.isString())
        {
            double k = 1.0;
            double b = 0.0;
            const QString text = tv.toString();
            if (foldLinearExpr(text, &k, &b))
            {
                seg.scale = k;
                seg.offset = b;
            }
            else
            {
                seg.transformExpr = parseExprText(text.toStdString(), err);
                if (!seg.transformExpr)
                    return false;
            }
        }
        else if (tv.isArray())
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
        else
        {
            err = "segment " + seg.name + " 的 transform 必须是表达式或算子数组";
            return false;
        }
    }

    // ---- expect：约束数组，逐项 AND ----
    if (o.contains(QStringLiteral("expect")))
    {
        const QJsonValue ev = o.value(QStringLiteral("expect"));
        if (!ev.isArray())
        {
            err = "segment " + seg.name + " 的 expect 必须是数组";
            return false;
        }
        if (!parseExpectArray(ev.toArray(), seg.checks, err))
            return false;
    }

    if (o.contains(QStringLiteral("if")))
    {
        seg.cond = jsonExpr(o.value(QStringLiteral("if")), err);
        if (!seg.cond)
            return false;
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
    t.mode = root.value(QStringLiteral("mode")).toString(QStringLiteral("bin")).toStdString();
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
    // 形态由顶层 mode 显式声明（SPEC §2.1.1），不再靠段特征推断。
    const bool docText = (t.mode == "text");

    // ---- v3 blocks：字段组。段用 type:"块名" 引用，编译期展开 ----
    if (root.contains(QStringLiteral("blocks")))
    {
        const QJsonObject bo = root.value(QStringLiteral("blocks")).toObject();
        for (auto it = bo.begin(); it != bo.end(); ++it)
        {
            const QJsonObject bod = it.value().toObject();
            BlockDef bd;
            bd.name = it.key().toStdString();
            bd.of = bod.value(QStringLiteral("of")).toString().toLower().toStdString();
            bd.repeater = bod.value(QStringLiteral("repeater")).toString().toStdString();
            bd.description = bod.value(QStringLiteral("description")).toString().toStdString();
            for (const QJsonValue &fv : bod.value(QStringLiteral("fields")).toArray())
            {
                SegSpec fs;
                if (!parseSegment(fv.toObject(), t.endian, fs, err, docText, &t.aliases))
                {
                    err = "blocks." + bd.name + "：" + err;
                    return false;
                }
                bd.fields.push_back(std::move(fs));
            }
            t.blocks[bd.name] = std::move(bd);
        }
    }

    // ---- v3 derived：顶层派生字段，按序求值，可引用前序派生 ----
    if (root.contains(QStringLiteral("derived")))
    {
        for (const QJsonValue &dv : root.value(QStringLiteral("derived")).toArray())
        {
            const QJsonObject dobj = dv.toObject();
            DerivedDef dd;
            dd.name = dobj.value(QStringLiteral("name")).toString().toStdString();
            dd.unit = dobj.value(QStringLiteral("unit")).toString().toStdString();
            dd.description = dobj.value(QStringLiteral("description")).toString().toStdString();
            {
                std::string ee;
                dd.expr = jsonExpr(dobj.value(QStringLiteral("expr")), ee);
                if (!dd.expr)
                {
                    err = "derived " + dd.name + " 的 expr 无效：" + ee;
                    return false;
                }
            }
            if (!parseEmit(dobj.value(QStringLiteral("output")).toString(), dd.emitMode, err))
                return false;
            t.derived.push_back(std::move(dd));
        }
    }

    for (const QJsonValue &sv : root.value(QStringLiteral("segments")).toArray())
    {
        SegSpec seg;
        if (!parseSegment(sv.toObject(), t.endian, seg, err, docText, &t.aliases))
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
                if (!parseSegment(fv.toObject(), t.endian, seg, err, docText, &t.aliases))
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
    const bool seq = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" || c.op == "sum8" ||
                      c.op == "sum16" || c.op == "xor8" || c.op == "parity");
    if (seq)
    {
        QString over;
        if (c.over.size() == 1)
            over = QString::fromStdString(c.over.front());
        else if (c.over.size() >= 2)
            over = QStringLiteral("%1:%2")
                       .arg(QString::fromStdString(c.over.front()), QString::fromStdString(c.over.back()));
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
    else if (c.op == "mask")
    {
        o["mask"] = exprJson(c.rhs);
        o["value"] = exprJson(c.rhs2);
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

// scale/offset 折回 transform 表达式：self*k+b，能省就省掉多余项。
static QString linearTransformText(double k, double b)
{
    auto num = [](double v) { return QString::number(v, 'g', 10); };
    if (k == 1.0)
        return b >= 0.0 ? QStringLiteral("self+%1").arg(num(b)) : QStringLiteral("self-%1").arg(num(-b));
    if (b == 0.0)
        return QStringLiteral("self*%1").arg(num(k));
    return b > 0.0 ? QStringLiteral("self*%1+%2").arg(num(k), num(b))
                   : QStringLiteral("self*%1-%2").arg(num(k), num(-b));
}

static QJsonObject segmentJson(const SegmentDef &seg, const ProtocolDocument &doc)
{
    QJsonObject o;
    o["name"] = QString::fromStdString(seg.name);
    const bool textSeg = (doc.mode == "text");
    const bool countIsOne = (seg.countExpr && seg.countExpr->kind == Expr::Num && seg.countExpr->isInt &&
                             seg.countExpr->ival == 1);

    auto writeCount = [&]() {
        if (seg.countWildcard)
            o["count"] = QStringLiteral("*");
        else if (!textSeg || !countIsOne)
            o["count"] = exprJson(seg.countExpr);
    };

    if (seg.bit.enabled)
    {
        o["type"] = (seg.bit.order == "msb0") ? QStringLiteral("rbit") : QStringLiteral("bit");
        o["count"] = QStringLiteral("%1@%2")
                         .arg(seg.bit.width)
                         .arg(static_cast<qlonglong>(seg.bit.offset));
    }
    else if (!seg.blockRef.empty())
    {
        o["type"] = QString::fromStdString(seg.blockRef);
        writeCount();
    }
    else
    {
        o["type"] = typeName(seg.type);
        writeCount();
    }

    if (textSeg)
    {
        if (!seg.format.empty())
            o["format"] = QString::fromStdString(seg.format);
        if (!seg.key.empty() && seg.literal.empty())
            o["key"] = QString::fromStdString(seg.key);
        if (!seg.literal.empty())
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
        if (!seg.emptyPolicy.empty())
            o["empty"] = QString::fromStdString(seg.emptyPolicy);
        if (!seg.pattern.empty())
            o["pattern"] = QString::fromStdString(seg.pattern);
    }

    if (!seg.unit.empty())
        o["unit"] = QString::fromStdString(seg.unit);
    if (!seg.description.empty())
        o["description"] = QString::fromStdString(seg.description);
    if (!seg.onMismatch.empty())
        o["on_mismatch"] = QString::fromStdString(seg.onMismatch);

    if (seg.emitMode == EmitMode::None)
        o["output"] = "none";
    else if (seg.emitMode == EmitMode::Aux)
        o["output"] = "aux";

    if (seg.transformExpr)
        o["transform"] = exprJson(seg.transformExpr);
    else if (!seg.transform.empty())
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
    else if (seg.scale != 1.0 || seg.offset != 0.0)
        o["transform"] = linearTransformText(seg.scale, seg.offset);

    if (seg.cond)
        o["if"] = exprJson(seg.cond);

    if (!seg.checks.empty())
    {
        QJsonArray expect;
        for (const auto &c : seg.checks)
            expect.append(checkJson(c));
        o["expect"] = expect;
    }
    return o;
}


QJsonObject toJson(const ProtocolDocument &doc)
{
    QJsonObject root;
    // version 键已从 schema 删除：回写不产出，避免存回来的文件过不了自己的校验
    root["name"] = QString::fromStdString(doc.name);
    if (!doc.description.empty())
        root["description"] = QString::fromStdString(doc.description);
    root["layout"] = QString::fromStdString(doc.layout);
    root["mode"] = QString::fromStdString(doc.mode);
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

    // v3 blocks：只有真的解析出块结构才回写，避免空对象污染输出
    if (!doc.blocks.empty())
    {
        QJsonObject blocks;
        for (const auto &kv : doc.blocks)
        {
            const BlockDef &bd = kv.second;
            QJsonObject bo;
            if (!bd.of.empty())
                bo["of"] = QString::fromStdString(bd.of);
            if (!bd.repeater.empty())
                bo["repeater"] = QString::fromStdString(bd.repeater);
            if (!bd.description.empty())
                bo["description"] = QString::fromStdString(bd.description);
            QJsonArray fields;
            for (const auto &f : bd.fields)
                fields.append(segmentJson(f, doc));
            bo["fields"] = fields;
            blocks[QString::fromStdString(bd.name)] = bo;
        }
        root["blocks"] = blocks;
    }

    // v3 derived：顶层派生字段，按声明顺序回写
    if (!doc.derived.empty())
    {
        QJsonArray dv;
        for (const auto &d : doc.derived)
        {
            QJsonObject o;
            o["name"] = QString::fromStdString(d.name);
            if (d.expr)
                o["expr"] = exprJson(d.expr);
            if (!d.unit.empty())
                o["unit"] = QString::fromStdString(d.unit);
            if (!d.description.empty())
                o["description"] = QString::fromStdString(d.description);
            if (d.emitMode == EmitMode::None)
                o["output"] = QStringLiteral("none");
            else if (d.emitMode == EmitMode::Aux)
                o["output"] = QStringLiteral("aux");
            dv.append(o);
        }
        root["derived"] = dv;
    }

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
