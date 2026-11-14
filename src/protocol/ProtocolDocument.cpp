#include "ProtocolDocument.h"
#include "ProtocolSpec.h"

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

namespace ps::protocol
{

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

static std::string numberKey(double v)
{
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return "n:" + os.str();
}

static std::string stringKey(const std::string &v) { return "s:" + v; }

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

bool parseDocument(const QJsonObject &root, ProtocolDocument &t, std::string &err)
{
    t.version = root.value(QStringLiteral("version")).toString(QStringLiteral("1")).toStdString();
    t.description = root.value(QStringLiteral("description")).toString().toStdString();
    t.layout = root.value(QStringLiteral("layout")).toString(QStringLiteral("sequential")).toStdString();
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

} // namespace ps::protocol
