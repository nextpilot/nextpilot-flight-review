#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace nextpilot::plotscope::protocol
{

// psproto 的值类型层。
//
// 设计原则（docs/roadmap/psproto-v2.md §10）：
//   - **值保持原类型**：段声明 u64，通道就是 u64，不存在"统一 double 视图"；
//   - 类型属于容器（同一列必然同类型），不属于元素 —— 所以是 TypedVec，
//     不是 vector<TypedValue>（后者每元素多带一份类型，破坏连续性与向量化）；
//   - 唯一允许的降级是显式 asDouble()，降级点可定位、可统计。
enum class ValueType : int
{
    U8 = 0,
    I8,
    U16,
    I16,
    U32,
    I32,
    U64,
    I64,
    F32,
    F64,
    Bool,
    Bcd,
    Text
};

inline bool isFloatType(ValueType t) { return t == ValueType::F32 || t == ValueType::F64; }
inline bool isSignedType(ValueType t)
{
    return t == ValueType::I8 || t == ValueType::I16 || t == ValueType::I32 ||
           t == ValueType::I64;
}
inline bool isIntegerType(ValueType t) { return t != ValueType::Text && !isFloatType(t); }

// 表达式求值用的标量（只用于瞬时计算，不存储）。字段分开存，避免 union 的类型双关风险。
struct TypedValue
{
    ValueType type = ValueType::F64;
    std::uint64_t u = 0;   // U8..U64 / Bool / Bcd
    std::int64_t i = 0;    // I8..I64
    double f = 0.0;        // F32 / F64
    std::string s;         // Text

    static TypedValue fromU(std::uint64_t v, ValueType t)
    {
        TypedValue r;
        r.type = t;
        r.u = v;
        return r;
    }
    static TypedValue fromI(std::int64_t v, ValueType t = ValueType::I64)
    {
        TypedValue r;
        r.type = t;
        r.i = v;
        return r;
    }
    static TypedValue fromF(double v, ValueType t = ValueType::F64)
    {
        TypedValue r;
        r.type = t;
        r.f = v;
        return r;
    }
    static TypedValue fromText(std::string v)
    {
        TypedValue r;
        r.type = ValueType::Text;
        r.s = std::move(v);
        return r;
    }

    bool isFloat() const { return isFloatType(type); }
    bool isSigned() const { return isSignedType(type); }

    double asDouble() const
    {
        if (type == ValueType::Text)
            return 0.0;
        if (isFloatType(type))
            return f;
        if (isSignedType(type))
            return static_cast<double>(i);
        return static_cast<double>(u);
    }
    // 整型域取值：浮点被截断（调用方需保证语义允许）
    std::int64_t asI64() const
    {
        if (isFloatType(type))
            return static_cast<std::int64_t>(f);
        if (isSignedType(type))
            return i;
        return static_cast<std::int64_t>(u);
    }
    std::uint64_t asU64() const
    {
        if (isFloatType(type))
            return static_cast<std::uint64_t>(f);
        if (isSignedType(type))
            return static_cast<std::uint64_t>(i);
        return u;
    }
};

// 一条通道 / 一个段的值列。同列同类型，零分支遍历。
class TypedVec
{
public:
    using VecU8 = std::vector<std::uint8_t>;
    using VecI8 = std::vector<std::int8_t>;
    using VecU16 = std::vector<std::uint16_t>;
    using VecI16 = std::vector<std::int16_t>;
    using VecU32 = std::vector<std::uint32_t>;
    using VecI32 = std::vector<std::int32_t>;
    using VecU64 = std::vector<std::uint64_t>;
    using VecI64 = std::vector<std::int64_t>;
    using VecF32 = std::vector<float>;
    using VecF64 = std::vector<double>;
    using VecText = std::vector<std::string>;

    using Storage = std::variant<VecU8, VecI8, VecU16, VecI16, VecU32, VecI32, VecU64, VecI64,
                                 VecF32, VecF64, VecText>;

    ValueType type = ValueType::F64;
    Storage data = VecF64{};

    TypedVec() = default;
    explicit TypedVec(ValueType t) { reset(t); }

    void reset(ValueType t)
    {
        type = t;
        switch (t)
        {
        case ValueType::U8: data = VecU8{}; break;
        case ValueType::I8: data = VecI8{}; break;
        case ValueType::U16: data = VecU16{}; break;
        case ValueType::I16: data = VecI16{}; break;
        case ValueType::U32: data = VecU32{}; break;
        case ValueType::I32: data = VecI32{}; break;
        case ValueType::U64: data = VecU64{}; break;
        case ValueType::I64: data = VecI64{}; break;
        case ValueType::F32: data = VecF32{}; break;
        case ValueType::F64: data = VecF64{}; break;
        case ValueType::Bool: data = VecU8{}; break;   // bool 存 1 字节
        case ValueType::Bcd: data = VecU32{}; break;   // BCD 解码成整数
        case ValueType::Text: data = VecText{}; break;
        }
    }

    static TypedVec make(ValueType t, std::size_t n = 0)
    {
        TypedVec v(t);
        v.reserve(n);
        return v;
    }

    std::size_t size() const
    {
        switch (type)
        {
        case ValueType::U8: return std::get<VecU8>(data).size();
        case ValueType::I8: return std::get<VecI8>(data).size();
        case ValueType::U16: return std::get<VecU16>(data).size();
        case ValueType::I16: return std::get<VecI16>(data).size();
        case ValueType::U32: return std::get<VecU32>(data).size();
        case ValueType::I32: return std::get<VecI32>(data).size();
        case ValueType::U64: return std::get<VecU64>(data).size();
        case ValueType::I64: return std::get<VecI64>(data).size();
        case ValueType::F32: return std::get<VecF32>(data).size();
        case ValueType::F64: return std::get<VecF64>(data).size();
        case ValueType::Bool: return std::get<VecU8>(data).size();
        case ValueType::Bcd: return std::get<VecU32>(data).size();
        case ValueType::Text: return std::get<VecText>(data).size();
        }
        return 0;
    }

    void reserve(std::size_t n)
    {
        switch (type)
        {
        case ValueType::U8: std::get<VecU8>(data).reserve(n); break;
        case ValueType::I8: std::get<VecI8>(data).reserve(n); break;
        case ValueType::U16: std::get<VecU16>(data).reserve(n); break;
        case ValueType::I16: std::get<VecI16>(data).reserve(n); break;
        case ValueType::U32: std::get<VecU32>(data).reserve(n); break;
        case ValueType::I32: std::get<VecI32>(data).reserve(n); break;
        case ValueType::U64: std::get<VecU64>(data).reserve(n); break;
        case ValueType::I64: std::get<VecI64>(data).reserve(n); break;
        case ValueType::F32: std::get<VecF32>(data).reserve(n); break;
        case ValueType::F64: std::get<VecF64>(data).reserve(n); break;
        case ValueType::Bool: std::get<VecU8>(data).reserve(n); break;
        case ValueType::Bcd: std::get<VecU32>(data).reserve(n); break;
        case ValueType::Text: std::get<VecText>(data).reserve(n); break;
        }
    }

    // 追加一个已解码的标量（按本列类型落位；不改变本列类型）
    void push(const TypedValue &v)
    {
        switch (type)
        {
        case ValueType::U8: std::get<VecU8>(data).push_back(static_cast<std::uint8_t>(v.asU64())); break;
        case ValueType::I8: std::get<VecI8>(data).push_back(static_cast<std::int8_t>(v.asI64())); break;
        case ValueType::U16: std::get<VecU16>(data).push_back(static_cast<std::uint16_t>(v.asU64())); break;
        case ValueType::I16: std::get<VecI16>(data).push_back(static_cast<std::int16_t>(v.asI64())); break;
        case ValueType::U32: std::get<VecU32>(data).push_back(static_cast<std::uint32_t>(v.asU64())); break;
        case ValueType::I32: std::get<VecI32>(data).push_back(static_cast<std::int32_t>(v.asI64())); break;
        case ValueType::U64: std::get<VecU64>(data).push_back(v.asU64()); break;
        case ValueType::I64: std::get<VecI64>(data).push_back(v.asI64()); break;
        case ValueType::F32: std::get<VecF32>(data).push_back(static_cast<float>(v.asDouble())); break;
        case ValueType::F64: std::get<VecF64>(data).push_back(v.asDouble()); break;
        case ValueType::Bool: std::get<VecU8>(data).push_back(v.asU64() ? 1 : 0); break;
        case ValueType::Bcd: std::get<VecU32>(data).push_back(static_cast<std::uint32_t>(v.asU64())); break;
        case ValueType::Text: std::get<VecText>(data).push_back(v.s); break;
        }
    }

    TypedValue at(std::size_t i) const
    {
        switch (type)
        {
        case ValueType::U8: return TypedValue::fromU(std::get<VecU8>(data)[i], type);
        case ValueType::I8: return TypedValue::fromI(std::get<VecI8>(data)[i], type);
        case ValueType::U16: return TypedValue::fromU(std::get<VecU16>(data)[i], type);
        case ValueType::I16: return TypedValue::fromI(std::get<VecI16>(data)[i], type);
        case ValueType::U32: return TypedValue::fromU(std::get<VecU32>(data)[i], type);
        case ValueType::I32: return TypedValue::fromI(std::get<VecI32>(data)[i], type);
        case ValueType::U64: return TypedValue::fromU(std::get<VecU64>(data)[i], type);
        case ValueType::I64: return TypedValue::fromI(std::get<VecI64>(data)[i], type);
        case ValueType::F32: return TypedValue::fromF(static_cast<double>(std::get<VecF32>(data)[i]), type);
        case ValueType::F64: return TypedValue::fromF(std::get<VecF64>(data)[i], type);
        case ValueType::Bool: return TypedValue::fromU(std::get<VecU8>(data)[i] ? 1 : 0, type);
        case ValueType::Bcd: return TypedValue::fromU(std::get<VecU32>(data)[i], type);
        case ValueType::Text: return TypedValue::fromText(std::get<VecText>(data)[i]);
        }
        return TypedValue{};
    }

    double asDouble(std::size_t i) const { return at(i).asDouble(); }
    std::int64_t asI64(std::size_t i) const { return at(i).asI64(); }
    std::uint64_t asU64(std::size_t i) const { return at(i).asU64(); }

    // 整列降级为 double（画图等消费端唯一入口）
    std::vector<double> toDoubleVec() const
    {
        std::vector<double> out;
        const std::size_t n = size();
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            out.push_back(asDouble(i));
        return out;
    }
};

} // namespace nextpilot::plotscope::protocol
