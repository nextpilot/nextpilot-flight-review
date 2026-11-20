#include "protocol/CompiledProgram.h"
#include "protocol/ProtocolSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace nextpilot::plotscope::protocol;

bool isTextVal(const TypedValue &v) { return v.type == ValueType::Text; }

bool intRes(std::int64_t v, bool asUnsigned, TypedValue &out)
{
    out = asUnsigned ? TypedValue::fromU(static_cast<std::uint64_t>(v), ValueType::U64)
                     : TypedValue::fromI(v, ValueType::I64);
    return true;
}

bool cmpNum(const TypedValue &a, const TypedValue &b, int &cmp, std::string &err)
{
    if (isTextVal(a) || isTextVal(b))
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
        cmp = (a.f < b.f) ? -1 : (a.f > b.f ? 1 : 0);
        return true;
    }
    if (a.isSigned() || b.isSigned())
    {
        const std::int64_t x = a.asI64(), y = b.asI64();
        cmp = (x < y) ? -1 : (x > y ? 1 : 0);
        return true;
    }
    const std::uint64_t x = a.asU64(), y = b.asU64();
    cmp = (x < y) ? -1 : (x > y ? 1 : 0);
    return true;
}

} // namespace

/* ---------------- 字节码执行 ---------------- */

bool nextpilot::plotscope::protocol::runByteCode(const ByteCode &bc,
                                                 const std::vector<TypedVec> &cols,
                                                 TypedValue &out, std::string &err)
{
    if (bc.constant)
    {
        out = bc.constValue;
        return true;
    }
    std::vector<TypedValue> st;
    st.reserve(bc.code.size() + 1);
    for (const Instr &in : bc.code)
    {
        switch (in.op)
        {
        case Instr::Const:
            st.push_back(in.imm);
            break;
        case Instr::Load:
        {
            if (in.slot < 0 || static_cast<size_t>(in.slot) >= cols.size())
            {
                err = "表达式引用了未定义的槽位";
                return false;
            }
            const TypedVec &col = cols[static_cast<size_t>(in.slot)];
            if (col.size() != 1)
            {
                err = "字段需要下标（数组），槽位 " + std::to_string(in.slot);
                return false;
            }
            st.push_back(col.at(0));
            break;
        }
        case Instr::LoadIdx:
        {
            if (st.empty())
            {
                err = "字节码栈下溢";
                return false;
            }
            const TypedValue idxv = st.back();
            st.pop_back();
            if (isTextVal(idxv) || isFloatType(idxv.type))
            {
                err = "下标必须是整型";
                return false;
            }
            const std::int64_t i = idxv.asI64();
            if (in.slot < 0 || static_cast<size_t>(in.slot) >= cols.size() || i < 0 ||
                static_cast<size_t>(i) >= cols[static_cast<size_t>(in.slot)].size())
            {
                err = "字段下标越界（槽位 " + std::to_string(in.slot) + "）";
                return false;
            }
            st.push_back(cols[static_cast<size_t>(in.slot)].at(static_cast<size_t>(i)));
            break;
        }
        case Instr::Neg:
        {
            const TypedValue a = st.back();
            st.pop_back();
            if (isTextVal(a))
            {
                err = "文本值不能取负";
                return false;
            }
            st.push_back(isFloatType(a.type) ? TypedValue::fromF(-a.f, ValueType::F64)
                                             : TypedValue::fromI(-a.asI64(), ValueType::I64));
            break;
        }
        case Instr::ToF64:
        case Instr::ToI64:
        case Instr::ToU64:
        {
            const TypedValue a = st.back();
            st.pop_back();
            if (isTextVal(a))
            {
                err = "类型转换不接受文本值";
                return false;
            }
            st.push_back(in.op == Instr::ToF64
                             ? TypedValue::fromF(a.asDouble(), ValueType::F64)
                             : (in.op == Instr::ToI64 ? TypedValue::fromI(a.asI64(), ValueType::I64)
                                                      : TypedValue::fromU(a.asU64(), ValueType::U64)));
            break;
        }
        case Instr::Abs:
        {
            const TypedValue a = st.back();
            st.pop_back();
            if (isTextVal(a))
            {
                err = "abs 不接受文本值";
                return false;
            }
            st.push_back(isFloatType(a.type)
                             ? TypedValue::fromF(std::fabs(a.f), ValueType::F64)
                             : TypedValue::fromI(a.asI64() < 0 ? -a.asI64() : a.asI64(),
                                                 ValueType::I64));
            break;
        }
        case Instr::Min:
        case Instr::Max:
        {
            if (st.size() < 2)
            {
                err = "字节码栈下溢";
                return false;
            }
            const TypedValue b = st.back();
            st.pop_back();
            const TypedValue a = st.back();
            st.pop_back();
            int cmp = 0;
            if (!cmpNum(a, b, cmp, err))
                return false;
            const bool takeA = (in.op == Instr::Min) ? (cmp <= 0) : (cmp >= 0);
            st.push_back(takeA ? a : b);
            break;
        }
        case Instr::Clamp:
        {
            if (st.size() < 3)
            {
                err = "字节码栈下溢";
                return false;
            }
            const TypedValue c = st.back();
            st.pop_back();
            const TypedValue b = st.back();
            st.pop_back();
            const TypedValue a = st.back();
            st.pop_back();
            int ab = 0, ac = 0;
            if (!cmpNum(a, b, ab, err) || !cmpNum(a, c, ac, err))
                return false;
            st.push_back((ab < 0) ? b : (ac > 0 ? c : a));
            break;
        }
        default:
        {
            if (st.size() < 2)
            {
                err = "字节码栈下溢";
                return false;
            }
            const TypedValue b = st.back();
            st.pop_back();
            const TypedValue a = st.back();
            st.pop_back();
            if (isTextVal(a) || isTextVal(b))
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
                switch (in.op)
                {
                case Instr::Add: st.push_back(TypedValue::fromF(x + y, ValueType::F64)); break;
                case Instr::Sub: st.push_back(TypedValue::fromF(x - y, ValueType::F64)); break;
                case Instr::Mul: st.push_back(TypedValue::fromF(x * y, ValueType::F64)); break;
                case Instr::Div:
                    if (y == 0.0) { err = "表达式除零"; return false; }
                    st.push_back(TypedValue::fromF(x / y, ValueType::F64));
                    break;
                case Instr::Lt: st.push_back(TypedValue::fromU(x < y, ValueType::Bool)); break;
                case Instr::Le: st.push_back(TypedValue::fromU(x <= y, ValueType::Bool)); break;
                case Instr::Gt: st.push_back(TypedValue::fromU(x > y, ValueType::Bool)); break;
                case Instr::Ge: st.push_back(TypedValue::fromU(x >= y, ValueType::Bool)); break;
                case Instr::Eq: st.push_back(TypedValue::fromU(x == y, ValueType::Bool)); break;
                case Instr::Ne: st.push_back(TypedValue::fromU(x != y, ValueType::Bool)); break;
                default: err = "浮点值不支持位运算/取模"; return false;
                }
                break;
            }
            const bool anySigned = a.isSigned() || b.isSigned();
            TypedValue r;
            if (anySigned)
            {
                const std::int64_t x = a.asI64(), y = b.asI64();
                switch (in.op)
                {
                case Instr::Add: intRes(x + y, false, r); break;
                case Instr::Sub: intRes(x - y, false, r); break;
                case Instr::Mul: intRes(x * y, false, r); break;
                case Instr::Div:
                    if (y == 0) { err = "表达式除零"; return false; }
                    intRes(x / y, false, r);
                    break;
                case Instr::Mod:
                    if (y == 0) { err = "表达式取模零"; return false; }
                    intRes(x % y, false, r);
                    break;
                case Instr::BitAnd: intRes(x & y, false, r); break;
                case Instr::BitOr: intRes(x | y, false, r); break;
                case Instr::BitXor: intRes(x ^ y, false, r); break;
                case Instr::Shl: intRes(x << y, false, r); break;
                case Instr::Shr: intRes(x >> y, false, r); break;
                case Instr::Lt: r = TypedValue::fromU(x < y, ValueType::Bool); break;
                case Instr::Le: r = TypedValue::fromU(x <= y, ValueType::Bool); break;
                case Instr::Gt: r = TypedValue::fromU(x > y, ValueType::Bool); break;
                case Instr::Ge: r = TypedValue::fromU(x >= y, ValueType::Bool); break;
                case Instr::Eq: r = TypedValue::fromU(x == y, ValueType::Bool); break;
                case Instr::Ne: r = TypedValue::fromU(x != y, ValueType::Bool); break;
                default: err = "未知运算符"; return false;
                }
            }
            else
            {
                const std::uint64_t x = a.asU64(), y = b.asU64();
                switch (in.op)
                {
                case Instr::Add: intRes(static_cast<std::int64_t>(x + y), true, r); break;
                case Instr::Sub: intRes(static_cast<std::int64_t>(x - y), true, r); break;
                case Instr::Mul: intRes(static_cast<std::int64_t>(x * y), true, r); break;
                case Instr::Div:
                    if (y == 0) { err = "表达式除零"; return false; }
                    intRes(static_cast<std::int64_t>(x / y), true, r);
                    break;
                case Instr::Mod:
                    if (y == 0) { err = "表达式取模零"; return false; }
                    intRes(static_cast<std::int64_t>(x % y), true, r);
                    break;
                case Instr::BitAnd: intRes(static_cast<std::int64_t>(x & y), true, r); break;
                case Instr::BitOr: intRes(static_cast<std::int64_t>(x | y), true, r); break;
                case Instr::BitXor: intRes(static_cast<std::int64_t>(x ^ y), true, r); break;
                case Instr::Shl: intRes(static_cast<std::int64_t>(x << y), true, r); break;
                case Instr::Shr: intRes(static_cast<std::int64_t>(x >> y), true, r); break;
                case Instr::Lt: r = TypedValue::fromU(x < y, ValueType::Bool); break;
                case Instr::Le: r = TypedValue::fromU(x <= y, ValueType::Bool); break;
                case Instr::Gt: r = TypedValue::fromU(x > y, ValueType::Bool); break;
                case Instr::Ge: r = TypedValue::fromU(x >= y, ValueType::Bool); break;
                case Instr::Eq: r = TypedValue::fromU(x == y, ValueType::Bool); break;
                case Instr::Ne: r = TypedValue::fromU(x != y, ValueType::Bool); break;
                default: err = "未知运算符"; return false;
                }
            }
            st.push_back(r);
            break;
        }
        }
        if (!err.empty())
            return false;
    }
    if (st.size() != 1)
    {
        err = "表达式求值栈不平衡";
        return false;
    }
    out = st.back();
    return true;
}

/* ---------------- 编译 ---------------- */

namespace {

using namespace nextpilot::plotscope::protocol;

struct Compiler
{
    const ProtocolDocument &doc;
    CompiledProgram &out;
    std::string &err;
    std::map<std::string, int32_t> slotOf; // 名字 → 槽位

    bool fail(const std::string &m)
    {
        err = m;
        return false;
    }

    int32_t slotIndex(const std::string &name)
    {
        auto it = slotOf.find(name);
        return it == slotOf.end() ? -1 : it->second;
    }

    bool emitExpr(const ExprPtr &e, ByteCode &bc, std::string &why)
    {
        if (!e)
            return fail("空表达式");
        switch (e->kind)
        {
        case Expr::Num:
        {
            Instr in;
            in.op = Instr::Const;
            in.imm = e->isInt ? TypedValue::fromI(e->ival, ValueType::I64)
                              : TypedValue::fromF(e->num, ValueType::F64);
            bc.code.push_back(in);
            return true;
        }
        case Expr::Ref:
        {
            const int32_t slot = slotIndex(e->name);
            if (slot < 0)
                return fail("表达式引用了未知段: " + e->name);
            if (e->b)
            {
                if (!emitExpr(e->b, bc, why))
                    return false;
                Instr in;
                in.op = Instr::LoadIdx;
                in.slot = slot;
                bc.code.push_back(in);
            }
            else
            {
                Instr in;
                in.op = Instr::Load;
                in.slot = slot;
                bc.code.push_back(in);
            }
            return true;
        }
        case Expr::Un:
        {
            if (!emitExpr(e->a, bc, why))
                return false;
            Instr in;
            in.op = Instr::Neg;
            bc.code.push_back(in);
            return true;
        }
        case Expr::Fn:
        {
            Instr in;
            if (e->fn == "toF64") in.op = Instr::ToF64;
            else if (e->fn == "toI64") in.op = Instr::ToI64;
            else if (e->fn == "toU64") in.op = Instr::ToU64;
            else if (e->fn == "abs") in.op = Instr::Abs;
            else if (e->fn == "min") in.op = Instr::Min;
            else if (e->fn == "max") in.op = Instr::Max;
            else if (e->fn == "clamp") in.op = Instr::Clamp;
            else return fail("未知函数: " + e->fn);

            if (!emitExpr(e->a, bc, why))
                return false;
            const bool needsB = (in.op == Instr::Min || in.op == Instr::Max || in.op == Instr::Clamp);
            const bool needsC = (in.op == Instr::Clamp);
            if (needsB && (!e->b || !emitExpr(e->b, bc, why)))
                return fail("函数参数不足: " + e->fn);
            if (needsC && (!e->c || !emitExpr(e->c, bc, why)))
                return fail("函数参数不足: " + e->fn);
            bc.code.push_back(in);
            return true;
        }
        case Expr::Bin:
        {
            if (!emitExpr(e->a, bc, why) || !emitExpr(e->b, bc, why))
                return false;
            Instr in;
            switch (e->op)
            {
            case '+': in.op = Instr::Add; break;
            case '-': in.op = Instr::Sub; break;
            case '*': in.op = Instr::Mul; break;
            case '/': in.op = Instr::Div; break;
            case '%': in.op = Instr::Mod; break;
            case '&': in.op = Instr::BitAnd; break;
            case '|': in.op = Instr::BitOr; break;
            case '^': in.op = Instr::BitXor; break;
            case 100: in.op = Instr::Shl; break;
            case 101: in.op = Instr::Shr; break;
            default: return fail("未知运算符");
            }
            bc.code.push_back(in);
            return true;
        }
        }
        return fail("表达式编译失败");
    }

    // 编译并尽量常量折叠：不含任何槽位访问的表达式在启动期算成常量。
    bool compileExpr(const ExprPtr &e, ByteCode &bc)
    {
        bc.code.clear();
        bc.constant = false;
        if (!e)
            return true;
        std::string why;
        if (!emitExpr(e, bc, why))
            return false;
        bool hasLoad = false;
        for (const Instr &in : bc.code)
            if (in.op == Instr::Load || in.op == Instr::LoadIdx)
                hasLoad = true;
        if (!hasLoad)
        {
            std::vector<TypedVec> dummy;
            TypedValue v;
            std::string why2;
            if (!runByteCode(bc, dummy, v, why2))
                return fail(why2);
            bc.constant = true;
            bc.constValue = v;
            bc.code.clear();
        }
        return true;
    }
};

// text 词法段编译（SPEC §7）：把候选定界符展开、字面量/重复/数值解释搬到段表，
// digest 的 from/to 端点在这里解析成段下标（-1 frame_start / -2 frame_end / -3 before:self）。
// 段在"记录里有字符边界段"的上下文里，且声明了任何字符语义键（进制/定宽/校验/模式/变换/
// 输出覆盖）→ 它就该按 token 解析，而不是按 type 宽度读字节。典型：NMEA 的 check 段
// （只有 base=16 + digest，没有 delimiter）。
bool compileTextSeg(const SegSpec &s, CompiledSeg &cs, Compiler &c)
{
    cs.boundary = static_cast<int>(boundaryOf(s));
    cs.key = s.key;
    if (s.hasWidth && (s.width == 0 || s.width > 65535))
        return c.fail("段 " + s.name + " 的 width 必须在 1..65535");
    if (!s.literal.empty() && (s.hasDelimiter || s.hasWidth || s.isRest))
        return c.fail("段 " + s.name + " 的 literal 与 delimiter/width/rest 互斥");
    if (s.derive)
    {
        if (!c.compileExpr(s.derive, cs.derive))
            return false;
        cs.hasDerive = true;
        cs.type = ValueType::F64;
        return true; // 派生字段不参与边界/字节布局
    }
    cs.isTime = s.type.isTime;
    cs.isTextSeg = (c.doc.mode == "text");
    if (!s.as.empty())
    {
        TypeInfo ti;
        std::string terr;
        if (!parseType(s.as, ti, terr))
            return c.fail("as 类型非法：" + s.as);
        cs.type = ti.type;
        cs.textType = (ti.type == ValueType::Text) ? 1 : 0;
    }
    if (cs.isTextSeg && cs.boundary == static_cast<int>(SegBoundary::Byte))
        cs.boundary = static_cast<int>(SegBoundary::Delimit); // 单 token（无定界符，靠后续边界/记录尾）
    if (!cs.isTextSeg)
        return true;
    cs.isLiteral = !s.literal.empty();
    cs.literal = s.literal;
    // format：由第一个取值转换符定出列类型与进制（%* 丢弃不算）
    if (!s.format.empty())
    {
        cs.textFormat = s.format;
        ValueType vt = ValueType::Text;
        for (size_t i = 0; i < s.format.size(); ++i)
        {
            if (s.format[i] != '%')
                continue;
            ++i;
            if (i >= s.format.size())
                break;
            if (s.format[i] == '%')
                continue;
            bool dis = false;
            if (s.format[i] == '*')
            {
                dis = true;
                ++i;
            }
            while (i < s.format.size() && std::isdigit(static_cast<unsigned char>(s.format[i])))
                ++i;
            if (i >= s.format.size())
                break;
            const char c = s.format[i];
            if (dis)
                continue;
            if (c == 'f' || c == 'e' || c == 'g' || c == 't' || c == 'T')
                vt = ValueType::F64;
            else if (c == 's' || c == 'c' || c == '[')
                vt = ValueType::Text;
            else
                vt = ValueType::I64;
            break;
        }
        cs.type = vt;
        // 整条 format 没有转换符 = 纯字面段：走字面匹配，同时保住帧首锚点（$）
        if (s.format.find('%') == std::string::npos)
        {
            cs.isLiteral = true;
            cs.literal = s.format;
            cs.boundary = static_cast<int>(SegBoundary::Literal);
        }
        // 进制由转换符决定：%x/%X → 16，%o → 8，其余一律十进制。
        // （文本段没有 base 键，别让 s.base 的默认值把这里推出的结果覆盖掉。）
        cs.base = (s.format.find("%x") != std::string::npos || s.format.find("%X") != std::string::npos)
                      ? 16
                      : (s.format.find("%o") != std::string::npos ? 8 : 10);
    }
    cs.textRepeat = s.countExpr && s.countExpr->kind == Expr::Num && s.countExpr->ival > 0
                        ? static_cast<uint32_t>(s.countExpr->ival)
                        : 1u;
    cs.hasDelimiter = s.hasDelimiter;
    for (const auto &d : s.delimiter)
    {
        if (d == "blank")
            cs.blankDelim = true;
        else
            cs.delimiters.push_back(d);
    }
    // 最长匹配优先：\r\n 必须先于 \n/\r 尝试，否则 CRLF 会被切成空字段。
    std::sort(cs.delimiters.begin(), cs.delimiters.end(),
              [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
    cs.repeatAll = (s.repeater == "*");
    cs.isRest = s.isRest;
    cs.hasWidth = s.hasWidth;
    cs.width = s.width;
    cs.textType = (s.type.type == ValueType::Text) ? 1 : 0;
    if (!cs.isTextSeg)
        cs.base = s.base;
    cs.hasFixedDigits = s.hasFixedDigits;
    cs.fixedDigits = s.fixedDigits;
    cs.signedFlag = s.signedFlag;
    cs.sci = s.sci;
    cs.emptyPolicy = s.emptyPolicy;
    cs.pattern = s.pattern;
    cs.onMismatch = (s.onMismatch == "warn") ? 1 : (s.onMismatch == "skip" ? 2 : 0);
    cs.transform = s.transform;
    if (!s.repeatMode.empty() && !cs.isTextSeg)
    {
        cs.repeatMode = (s.repeatMode == "eos") ? 1 : 2;
        if (cs.repeatMode == 2 && !c.compileExpr(s.repeatUntil, cs.repeatUntil))
            return false;
    }
    cs.hasRange = s.hasRange;
    cs.rangeMin = s.rangeMin;
    cs.rangeMax = s.rangeMax;
    cs.hasEnum = s.hasEnum;
    cs.enumValues = s.enumValues;
    if (s.hasRange && s.rangeMin > s.rangeMax)
        return c.fail("段 " + s.name + " 的 range.min > range.max");
    if (s.hasEnum && s.enumValues.empty())
        return c.fail("段 " + s.name + " 的 enum 为空");
    if (s.digest.enabled)
    {
        cs.digestKind = s.digest.alg.empty() ? 2 : 1;
        cs.digestAlg = s.digest.alg;
        cs.digestScript = s.digest.script;
        cs.digestFrom = -1;
        cs.digestTo = -3;
        if (s.digest.from.rfind("after:", 0) == 0)
        {
            const std::string ref = s.digest.from.substr(6);
            cs.digestFrom = c.slotIndex(ref);
            if (cs.digestFrom < 0)
                return c.fail("段 " + s.name + " 的 digest.from 未找到段：" + ref);
        }
        if (s.digest.to == "frame_end")
            cs.digestTo = -2;
        else if (s.digest.to.rfind("before:", 0) == 0)
        {
            const std::string ref = s.digest.to.substr(7);
            if (ref == s.name)
                cs.digestTo = -3;
            else
            {
                cs.digestTo = c.slotIndex(ref);
                if (cs.digestTo < 0)
                    return c.fail("段 " + s.name + " 的 digest.to 未找到段：" + ref);
            }
        }
        cs.digestHasInject = s.digest.hasInject;
        cs.digestInject = s.digest.inject;
        cs.hasDigestBounds = true;
    }
    return true;
}

} // namespace

// ---- v3 块展开 ----
static ExprPtr constCountExpr(int64_t n)
{
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Num;
    e->isInt = true;
    e->ival = n;
    return e;
}

// 把 segments 里"type 指向块名"的段摊平成普通段，后续编译/解码无需知道块的存在。
//   位域块（of 非空）：先造一个读容器的占位段（不出通道），再让每个字段的位窗口相对容器起点；
//   顺序块（of 为空）：字段按序读字节，通道名摊成 段名.字段名。
// 常量 count=N 摊成 N 份，命名 段名[i].字段名（对应界面里的 payload[i].a）。
// count:"*"（整组重复到帧尾）需要组循环，尚未接入，这里明确报错而不是静默按 1 处理。
// 深拷贝表达式树，并把指向 from 的 Ref 换名成 to。
// v3 的 transform 表达式里 self 指本段原始值：编译成字节码时直接换成该段自己的槽位名。
static ExprPtr renameRefs(const ExprPtr &e, const std::string &from, const std::string &to)
{
    if (!e)
        return e;
    auto n = std::make_shared<Expr>();
    *n = *e;
    n->name = (e->kind == Expr::Ref && e->name == from) ? to : e->name;
    n->a = renameRefs(e->a, from, to);
    n->b = renameRefs(e->b, from, to);
    n->c = renameRefs(e->c, from, to);
    return n;
}

// transform 表达式一律按浮点算：物理量本来就是实数，
// 否则 (self-32)*5/9 会走整型整除，5/9 直接变 0。字面量统一转浮点。
static ExprPtr floatize(const ExprPtr &e)
{
    if (!e)
        return e;
    auto n = std::make_shared<Expr>();
    *n = *e;
    if (e->kind == Expr::Num && e->isInt)
    {
        n->isInt = false;
        n->num = static_cast<double>(e->ival);
    }
    n->a = floatize(e->a);
    n->b = floatize(e->b);
    n->c = floatize(e->c);
    return n;
}

struct FlatGroup
{
    size_t first = 0;
    size_t count = 0;
    uint32_t bytes = 0;
};

static bool expandBlockSegs(const ProtocolDocument &doc, std::vector<SegSpec> &out,
                            std::vector<FlatGroup> &groups, std::string &err)
{
    for (const SegSpec &s : doc.segments)
    {
        if (s.blockRef.empty())
        {
            out.push_back(s);
            continue;
        }
        auto it = doc.blocks.find(s.blockRef);
        if (it == doc.blocks.end())
        {
            err = "段 " + s.name + " 引用的块不存在：" + s.blockRef;
            return false;
        }
        const BlockDef &b = it->second;
        if (b.fields.empty())
        {
            err = "块 " + b.name + " 没有字段";
            return false;
        }
        // 组重复由块自己的 repeater:"*" 声明，段不再写 count:"*"（重复不算字段数）。
        const bool repeats = (b.repeater == "*");
        if (s.countWildcard)
        {
            err = repeats ? "段 " + s.name + "：块已用 repeater 声明重复，不要再写 count:\"*\""
                          : "段 " + s.name + "：组重复请写在块的 repeater 上，段的 count:\"*\" 对块无效";
            return false;
        }
        int64_t reps = 1;
        if (s.countExpr)
        {
            if (s.countExpr->kind != Expr::Num || !s.countExpr->isInt || s.countExpr->ival <= 0)
            {
                err = "段 " + s.name + " 引用块时 count 只能是正整数常量";
                return false;
            }
            reps = s.countExpr->ival;
        }
        if (repeats && reps != 1)
        {
            err = "段 " + s.name + "：块已声明 repeater，不能再写 count";
            return false;
        }
        if (reps > 100000)
        {
            err = "段 " + s.name + " 引用块时 count 过大";
            return false;
        }

        // 一组占多少字节：位域块就是容器宽度；顺序块是各字段定长之和。
        TypeInfo containerTi;
        uint32_t groupBytes = 0;
        if (!b.of.empty())
        {
            std::string terr;
            if (!parseType(b.of, containerTi, terr) || containerTi.size == 0)
            {
                err = "块 " + b.name + " 的 of 类型非法：" + b.of;
                return false;
            }
            groupBytes = containerTi.size;
        }
        else
        {
            for (const SegSpec &f : b.fields)
            {
                if (f.bit.enabled)
                {
                    err = "块 " + b.name + " 的字段 " + f.name + " 是位域字段，顺序块不能用";
                    return false;
                }
                if (!f.countExpr || f.countExpr->kind != Expr::Num || !f.countExpr->isInt ||
                    f.countExpr->ival <= 0)
                {
                    err = "块 " + b.name + " 的字段 " + f.name + " 不是定长，不能作为重复组";
                    return false;
                }
                groupBytes += static_cast<uint32_t>(f.countExpr->ival * f.type.size);
            }
            if (groupBytes == 0)
            {
                err = "块 " + b.name + " 占 0 字节，无法重复";
                return false;
            }
        }

        const size_t startIdx = out.size();
        for (int64_t r = 0; r < reps; ++r)
        {
            const std::string inst = (reps == 1) ? s.name : s.name + "[" + std::to_string(r) + "]";
            if (!b.of.empty())
            {
                SegSpec cont;
                cont.name = inst;
                cont.type = containerTi;
                cont.countExpr = constCountExpr(1);
                cont.bigEndian = s.bigEndian;
                cont.emitMode = EmitMode::None;
                cont.prefix = s.prefix;
                out.push_back(cont);
            }
            for (const SegSpec &f : b.fields)
            {
                SegSpec fs = f;
                fs.name = inst + "." + f.name;
                fs.prefix = s.prefix;
                if (!b.of.empty())
                    fs.bit.base = inst; // 位窗口偏移相对容器起点
                out.push_back(fs);
            }
        }
        if (repeats)
            groups.push_back(FlatGroup{startIdx, out.size() - startIdx, groupBytes});
    }
    return true;
}

bool nextpilot::plotscope::protocol::compileProgram(const ProtocolDocument &doc,
                                                    CompiledProgram &out, std::string &err)
{
    out = CompiledProgram{};
    out.maxsize = doc.maxsize;
    Compiler c{doc, out, err, {}};

    // 0) 块展开：把 type 指向块的段摊平成普通段，后面所有环节只认摊平结果。
    std::vector<SegSpec> flat;
    std::vector<FlatGroup> groups;
    if (!expandBlockSegs(doc, flat, groups, err))
        return false;

    // v3 顶层 derived：摊成"派生段"追加到末尾——不读字节、不进边界计算，按序求值。
    for (const DerivedDef &dd : doc.derived)
    {
        SegSpec ds;
        ds.name = dd.name;
        ds.derive = dd.expr;
        ds.unit = dd.unit;
        ds.description = dd.description;
        ds.emitMode = dd.emitMode;
        flat.push_back(std::move(ds));
    }

    // 1) 槽位：顶层段在前，变体段按出现顺序追加。表达式里能用到的名字都在这里解析。
    for (const SegSpec &s : flat)
    {
        CompiledSlot slot;
        slot.name = s.name;
        slot.type = s.type.type;
        slot.elemSize = s.type.size;
        slot.bigEndian = s.bigEndian;
        c.slotOf[s.name] = static_cast<int32_t>(out.slotTable.size());
        // 块内字段摊平后名字是 块名.字段名，表达式里只认裸字段名：补一条别名指到同一槽位。
        // 同一裸名出现多次时以最早的那个实例为准（与校验端的注册顺序一致）。
        const size_t dot = s.name.rfind('.');
        if (dot != std::string::npos)
        {
            const std::string bare = s.name.substr(dot + 1);
            if (c.slotOf.find(bare) == c.slotOf.end())
                c.slotOf[bare] = static_cast<int32_t>(out.slotTable.size());
        }
        out.slotTable.push_back(slot);
    }
    for (const VariantSpec &v : doc.series.variants)
        for (const SegSpec &s : v.segments)
        {
            CompiledSlot slot;
            slot.name = s.name;
            slot.type = s.type.type;
            slot.elemSize = s.type.size;
            slot.bigEndian = s.bigEndian;
            c.slotOf[s.name] = static_cast<int32_t>(out.slotTable.size());
            out.slotTable.push_back(slot);
        }

    // 2) 顶层段表（摊平后）。
    for (size_t fi = 0; fi < flat.size(); ++fi)
    {
        const SegSpec &s = flat[fi];
        CompiledSeg cs;
        cs.name = s.name;
        cs.unit = s.unit;
        cs.prefix = "frame." + s.prefix;
        cs.slot = c.slotIndex(s.name);
        cs.type = s.type.type;
        cs.elemSize = s.type.size;
        cs.bigEndian = s.bigEndian;
        cs.scale = s.scale;
        cs.offset = s.offset;
        cs.identityPhys = (s.scale == 1.0 && s.offset == 0.0);
        cs.emitMode = (s.emitMode == EmitMode::Channel) ? 0 : (s.emitMode == EmitMode::Aux ? 1 : 2);
        // 段级降级策略与文本词法参数在 bin 段同样生效（此前只在 text 编译函数里赋值）。
        cs.onMismatch = (s.onMismatch == "warn") ? 1 : (s.onMismatch == "skip" ? 2 : 0);
        cs.pattern = s.pattern;
        cs.emptyPolicy = s.emptyPolicy;
        // repeat 同样是"仅 bin"键，此前只在 text 编译函数里编译 → bin 段 repeat 一直静默为 0。
        if (!s.repeatMode.empty())
        {
            cs.repeatMode = (s.repeatMode == "eos") ? 1 : 2;
            if (cs.repeatMode == 2 && !c.compileExpr(s.repeatUntil, cs.repeatUntil))
                return false;
        }
        cs.isText = (s.type.type == ValueType::Text);
        if (!compileTextSeg(s, cs, c))
            return false;

        // 位域：不占独立字节，count 恒为 1，值从容器（或帧首）的位窗口取。
        if (s.bit.enabled)
        {
            cs.hasBit = true;
            cs.bitOffset = s.bit.offset;
            cs.bitWidth = static_cast<uint8_t>(s.bit.width);
            cs.bitMsb0 = (s.bit.order == "msb0");
            cs.bitSigned = isSignedType(s.type.type);
            cs.bitBaseSlot = -1;
            if (!s.bit.base.empty())
            {
                const int32_t idx = c.slotIndex(s.bit.base);
                if (idx < 0)
                {
                    err = "段 " + s.name + " 的 bit.base 未定义：" + s.bit.base;
                    return false;
                }
                cs.bitBaseSlot = idx;
            }
            cs.countIsConst = true;
            cs.constCount = 1;
            cs.constBytes = 0;
        }

        // 非线性 transform：解析期折不成 self*k+b 的表达式，逐帧求值。
        if (s.transformExpr)
        {
            cs.hasTransformExpr = true;
            cs.identityPhys = false;
            if (!c.compileExpr(floatize(renameRefs(s.transformExpr, "self", s.name)), cs.transformCode))
                return false;
        }

        // 段级 if：条件不成立就跳过该段。
        if (s.cond)
        {
            cs.hasCond = true;
            if (!c.compileExpr(s.cond, cs.cond))
                return false;
        }

        if (!cs.hasBit && !c.compileExpr(s.countExpr, cs.count))
            return false;
        if (!cs.hasBit && cs.count.constant)
        {
            const int64_t v = cs.count.constValue.isSigned() ? cs.count.constValue.asI64()
                                                             : static_cast<int64_t>(cs.count.constValue.asU64());
            if (v <= 0 || v > 1000000)
                return c.fail("段 " + s.name + " 的 count 非法");
            cs.countIsConst = true;
            cs.constCount = static_cast<uint32_t>(v);
            // text 段的元素个数由定界符决定，不占"字节区间"，constBytes 恒为 0。
            cs.constBytes = cs.isTextSeg ? 0 : cs.constCount * s.type.size;
            if (!cs.isTextSeg && (cs.constBytes == 0 || cs.constBytes > (1u << 20)))
                return c.fail("段 " + s.name + " 字节数非法");
        }
        else
        {
            cs.countIsConst = false;
        }

        if (s.expectExpr)
        {
            cs.hasExpect = true;
            if (!c.compileExpr(s.expectExpr, cs.expect))
                return false;
            if (cs.expect.constant)
            {
                cs.expectIsConst = true;
                cs.expectConst = cs.expect.constValue;
            }
        }

        for (const CheckSpec &k : s.checks)
        {
            CompiledCheck cc;
            cc.op = k.op;
            cc.values = k.values;
            cc.poly = k.poly;
            cc.init = k.init;
            cc.xorout = k.xorout;
            cc.refin = k.refin;
            cc.refout = k.refout;
            cc.width = k.width;
            cc.isSequence = (k.op == "crc8" || k.op == "crc16" || k.op == "crc32" ||
                             k.op == "sum8" || k.op == "sum16" || k.op == "xor8" || k.op == "parity");
            cc.isMask = (k.op == "mask");
            if (cc.isSequence)
            {
                // over 在这里一次性解析成槽位序列；payload 用 -1 标记（读帧时按连续字节取）。
                std::set<std::string> seen;
                std::vector<std::string> order;
                for (const SegSpec &x : doc.segments)
                    order.push_back(x.name);
                for (const std::string &token : k.over)
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
                            return c.fail("over 区间非法: " + token);
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
                        if (doc.series.enabled && n == doc.series.payload)
                            cc.overSlots.push_back(-1);
                        else
                        {
                            const int32_t slot = c.slotIndex(n);
                            if (slot < 0)
                                return c.fail("over 引用未知段: " + n);
                            cc.overSlots.push_back(slot);
                        }
                    }
                }
            }
            else
            {
                if (k.rhs && !c.compileExpr(k.rhs, cc.rhs))
                    return false;
                if (k.rhs2 && !c.compileExpr(k.rhs2, cc.rhs2))
                    return false;
            }
            cs.checks.push_back(std::move(cc));
        }

        if (s.length.enabled)
        {
            cs.length.enabled = true;
            cs.length.bytesUnit = (s.length.unit == "bytes");
            for (const std::string &fn : s.length.fields)
            {
                if (doc.series.enabled && fn == doc.series.payload)
                {
                    cs.length.fields.push_back(-1);
                    continue;
                }
                const int32_t slot = c.slotIndex(fn);
                if (slot < 0)
                    return c.fail("length 引用未知段: " + fn);
                cs.length.fields.push_back(slot);
            }
            if (s.length.expr && !c.compileExpr(s.length.expr, cs.length.expr))
                return false;
        }
        for (const FlatGroup &g : groups)
        {
            if (fi >= g.first && fi < g.first + g.count)
            {
                cs.groupFirst = static_cast<int32_t>(g.first);
                cs.groupLast = static_cast<int32_t>(g.first + g.count - 1);
                cs.groupBytes = g.bytes;
                break;
            }
        }
        out.segs.push_back(std::move(cs));
    }

    // 3) 变体段表（扁平）
    for (const VariantSpec &v : doc.series.variants)
    {
        CompiledVariant cv;
        cv.name = v.name;
        for (const std::string &k : v.keys)
            cv.keys.push_back(k);
        for (const SegSpec &s : v.segments)
        {
            CompiledSeg cs;
            cs.name = s.name;
            cs.unit = s.unit;
            cs.prefix = "frame." + v.name + ".";
            cs.slot = c.slotIndex(s.name);
            cs.type = s.type.type;
            cs.elemSize = s.type.size;
            cs.bigEndian = s.bigEndian;
            cs.scale = s.scale;
            cs.offset = s.offset;
            cs.identityPhys = (s.scale == 1.0 && s.offset == 0.0);
            cs.emitMode = (s.emitMode == EmitMode::Channel) ? 0 : (s.emitMode == EmitMode::Aux ? 1 : 2);
            cs.onMismatch = (s.onMismatch == "warn") ? 1 : (s.onMismatch == "skip" ? 2 : 0);
            cs.pattern = s.pattern;
            cs.emptyPolicy = s.emptyPolicy;
            if (!s.repeatMode.empty())
            {
                cs.repeatMode = (s.repeatMode == "eos") ? 1 : 2;
                if (cs.repeatMode == 2 && !c.compileExpr(s.repeatUntil, cs.repeatUntil))
                    return false;
            }
            cs.isText = (s.type.type == ValueType::Text);
            if (!compileTextSeg(s, cs, c))
                return false;

                    // 位域：不占独立字节，count 恒为 1，值从容器（或帧首）的位窗口取。
                    if (s.bit.enabled)
                    {
                cs.hasBit = true;
                cs.bitOffset = s.bit.offset;
                cs.bitWidth = static_cast<uint8_t>(s.bit.width);
                cs.bitMsb0 = (s.bit.order == "msb0");
                cs.bitSigned = isSignedType(s.type.type);
                cs.bitBaseSlot = -1;
                        if (!s.bit.base.empty())
                        {
                            const int32_t idx = c.slotIndex(s.bit.base);
                            if (idx < 0)
                            {
                                err = "段 " + s.name + " 的 bit.base 未定义：" + s.bit.base;
                                return false;
                            }
                    cs.bitBaseSlot = idx;
                        }
                cs.countIsConst = true;
                cs.constCount = 1;
                cs.constBytes = 0;
                    }

                    // 段级 if：条件不成立就跳过该段。
                    if (s.cond)
                    {
                cs.hasCond = true;
                        if (!c.compileExpr(s.cond, cs.cond))
                            return false;
                    }
            if (!cs.hasBit && !c.compileExpr(s.countExpr, cs.count))
                return false;
            if (!cs.hasBit && cs.count.constant)
            {
                const int64_t n = cs.count.constValue.isSigned()
                                      ? cs.count.constValue.asI64()
                                      : static_cast<int64_t>(cs.count.constValue.asU64());
                if (n <= 0 || n > 1000000)
                    return c.fail("变体段 " + s.name + " 的 count 非法");
        cs.countIsConst = true;
                cs.constCount = static_cast<uint32_t>(n);
                cs.constBytes = cs.isTextSeg ? 0 : cs.constCount * s.type.size;
            }
            if (s.expectExpr)
            {
                cs.hasExpect = true;
                if (!c.compileExpr(s.expectExpr, cs.expect))
                    return false;
                if (cs.expect.constant)
                {
                    cs.expectIsConst = true;
                    cs.expectConst = cs.expect.constValue;
                }
            }
            cv.segs.push_back(static_cast<int32_t>(out.varSegs.size()));
            out.varSegs.push_back(std::move(cs));
        }
        out.variants.push_back(std::move(cv));
    }

    // 4) series / timestamp
    out.hasSeries = doc.series.enabled;
    if (doc.series.enabled)
    {
        out.payloadSlot = c.slotIndex(doc.series.payload);
        out.selectorSlot = doc.series.selector ? c.slotIndex(doc.series.selector->name) : -1;
        out.fallbackReject = (doc.series.fallback == "reject");
        if (out.selectorSlot < 0)
            return c.fail("series.selector 引用了未知段");
        if (out.payloadSlot < 0)
            return c.fail("series.payload 引用了未知段");
    }

    // 记录层形态由顶层 mode 决定（SPEC §2.1.1），不靠段特征推断。
    out.isText = (doc.mode == "text");
    // 有段用定界符切 token → 记录按"切分"解析（与是否称它为 text 无关）。
    for (const CompiledSeg &cs : out.segs)
        if (cs.hasDelimiter || cs.boundary == static_cast<int>(SegBoundary::Format))
            out.isDelimited = true;
    out.hasTerminator = doc.hasTerminator;
    out.terminator = doc.terminator;
    // 帧首字面量 = 第一个 literal 段的值；流式定位靠它（NMEA 的 "$"）。
    for (const CompiledSeg &cs : out.segs)
    {
        if (cs.isLiteral && !cs.literal.empty())
        {
            out.hasStartAnchor = true;
            out.startAnchor = cs.literal;
            break;
        }
    }
    // 首段用 expect 判等且值是字面串时，它也是帧首识别点（NMEA 除了 "$" 还有 "$GPGGA" 这种）。
    if (out.isText && !out.hasStartAnchor && !out.segs.empty())
    {
        const CompiledSeg &first = out.segs.front();
        for (const CompiledCheck &ck : first.checks)
        {
            if (ck.op != "eq" || ck.values.size() != 1 ||
                ck.values.front().type != ValueType::Text)
                continue;
            if (ck.values.front().s.size() >= 2)
            {
                out.hasStartAnchor = true;
                out.startAnchor = ck.values.front().s;
            }
            break;
        }
    }
    // text 也必须先能定位帧首：锚点（首段字面 / expect 判等）或记录终止符，二者至少一个。
    // 否则等于在流里任意偏移直接 scanf，会把垃圾数据读成字段。
    if (out.isText && !out.hasStartAnchor && !out.hasTerminator)
    {
        err = "text 协议必须先能定位帧首：首段写字面（如 \"$\"）、首段 expect 判等，或顶层写 terminator";
        return false;
    }
    out.hasTimestamp = doc.hasTimestamp && doc.timestampExpr;
    if (out.hasTimestamp)
    {
        if (!c.compileExpr(doc.timestampExpr, out.timestamp))
            return false;
        out.timestampUnit = doc.timestampUnit;
        out.timestampOrigin = doc.timestampOrigin;
        out.timestampRaw = doc.timestampRaw;
        out.timestampIndexFallback = (doc.timestampFallback == "index");
    }
    return true;
}
