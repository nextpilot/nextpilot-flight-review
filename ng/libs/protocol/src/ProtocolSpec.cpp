#include "protocol/ProtocolSpec.h"

#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::protocol
{

QStringList typeNames()
{
    return {QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"), QStringLiteral("i16"),
            QStringLiteral("u32"), QStringLiteral("i32"), QStringLiteral("u64"), QStringLiteral("i64"),
            QStringLiteral("f32"), QStringLiteral("f64"), QStringLiteral("bool"),
            QStringLiteral("bcd"), QStringLiteral("text"), QStringLiteral("str"),
            QStringLiteral("string"), QStringLiteral("ascii"), QStringLiteral("char")};
}

// v2 `count` 优先，v1 `size` 兜底。
static QJsonValue segCount(const QJsonObject &s)
{
    return s.contains(QLatin1String("count")) ? s.value(QLatin1String("count"))
                                              : s.value(QLatin1String("size"));
}

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
    if (t == "text" || t == "str" || t == "string" || t == "ascii" || t == "char")
    { ti = {FType::STR, 1, false, false, false}; return true; }
    err = "未知类型: " + raw;
    return false;
}


int typeBytes(const QString &type)
{
    const QString t = type.toLower();
    if (t.startsWith(QLatin1String("u16")) || t.startsWith(QLatin1String("i16"))) return 2;
    if (t.startsWith(QLatin1String("u32")) || t.startsWith(QLatin1String("i32")) || t.startsWith(QLatin1String("f32"))) return 4;
    if (t.startsWith(QLatin1String("u64")) || t.startsWith(QLatin1String("i64")) || t.startsWith(QLatin1String("f64"))) return 8;
    return 1;
}

bool literalSize(const QJsonValue &v, int *out)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (d != std::trunc(d) || d < 1 || d > 65535)
        return false;
    *out = static_cast<int>(d);
    return true;
}

int operatorWidth(const QString &op)
{
    if (op == QLatin1String("crc16") || op == QLatin1String("sum16")) return 2;
    if (op == QLatin1String("crc32")) return 4;
    if (op == QLatin1String("crc8") || op == QLatin1String("sum8") ||
        op == QLatin1String("xor8") || op == QLatin1String("parity")) return 1;
    return 0;
}

QStringList validate(const QJsonObject &o)
{
    QStringList errs;
    if (o.isEmpty())
    {
        errs << QStringLiteral("JSON 为空或无法解析");
        return errs;
    }
    if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        errs << QStringLiteral("协议 name 不能为空");
    if (o.value(QStringLiteral("layout")).toString() != QLatin1String("sequential"))
        errs << QStringLiteral("新协议 layout 必须是 sequential");
    const int maxsize = o.value(QStringLiteral("maxsize")).toInt(4096);
    if (maxsize < 2 || maxsize > 65535)
        errs << QStringLiteral("maxsize 必须在 2..65535");
    const QJsonArray segArr = o.value(QStringLiteral("segments")).toArray();
    if (segArr.isEmpty())
        errs << QStringLiteral("segments 不能为空");
    {
        const QString endian = o.value(QStringLiteral("endian")).toString();
        if (o.contains(QStringLiteral("endian")) && endian != QLatin1String("big") &&
            endian != QLatin1String("little") && endian != QLatin1String("be") &&
            endian != QLatin1String("le"))
            errs << QStringLiteral("endian 只能是 little/big（别名 le/be，解析层归一）");
    }

    QSet<QString> names;
    QMap<QString, int> order;
    QMap<QString, int> fixedBytes;
    QMap<QString, QString> roles;
    int index = 0;
    for (const QJsonValue &sv : segArr)
    {
        const QJsonObject seg = sv.toObject();
        const QString name = seg.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
        {
            errs << QStringLiteral("第 %1 段缺少 name").arg(index + 1);
            ++index;
            continue;
        }
        if (names.contains(name))
            errs << QStringLiteral("段名重复：%1").arg(name);
        names.insert(name);
        order.insert(name, index++);
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(name).hasMatch())
            errs << QStringLiteral("段名只能是标识符：%1").arg(name);
        int n = 0;
        if (literalSize(segCount(seg), &n))
            fixedBytes.insert(name, n * typeBytes(seg.value(QStringLiteral("type")).toString()));
        else if (!segCount(seg).isString() ||
                 segCount(seg).toString().trimmed().isEmpty())
            errs << QStringLiteral("%1 的 count 必须是正整数或表达式").arg(name);
        const QString type = seg.value(QStringLiteral("type")).toString().toLower();
        if (!typeNames().contains(type))
            errs << QStringLiteral("%1 的类型未知：%2").arg(name, type);
        const QString role = seg.value(QStringLiteral("role")).toString();
        if (!role.isEmpty())
        {
            const QStringList allowed = {QStringLiteral("sync"), QStringLiteral("length"),
                QStringLiteral("payload"), QStringLiteral("checksum"), QStringLiteral("data")};
            if (!allowed.contains(role))
                errs << QStringLiteral("%1 的 role 未知：%2").arg(name, role);
            else if ((role == QLatin1String("length") || role == QLatin1String("payload") ||
                      role == QLatin1String("checksum")) && roles.contains(role))
                errs << QStringLiteral("role 重复：%1").arg(role);
            else
                roles.insert(role, name);
            if (role == QLatin1String("payload") && type != QLatin1String("u8"))
                errs << QStringLiteral("payload 段 %1 必须是 u8").arg(name);
            if (role == QLatin1String("length") && segCount(seg).isDouble() &&
                segCount(seg).toDouble() > 1)
                errs << QStringLiteral("length 段 %1 应为单元素").arg(name);
        }
        if ((seg.contains(QStringLiteral("expect")) || seg.contains(QStringLiteral("value"))) &&
            (seg.contains(QStringLiteral("expect")) ? seg.value(QStringLiteral("expect")) : seg.value(QStringLiteral("value"))).isUndefined())
            errs << QStringLiteral("%1 的 value 无效").arg(name);

        const QJsonArray checks = seg.value(QStringLiteral("checks")).toArray();
        for (const QJsonValue &cv : checks)
        {
            const QJsonObject c = cv.toObject();
            const QString op = c.value(QStringLiteral("op")).toString(QStringLiteral("eq"));
            const QStringList pred = {QStringLiteral("eq"), QStringLiteral("ne"), QStringLiteral("lt"),
                QStringLiteral("le"), QStringLiteral("gt"), QStringLiteral("ge"), QStringLiteral("in"),
                QStringLiteral("range"), QStringLiteral("mask")};
            const QStringList seq = {QStringLiteral("crc8"), QStringLiteral("crc16"), QStringLiteral("crc32"),
                QStringLiteral("sum8"), QStringLiteral("sum16"), QStringLiteral("xor8"), QStringLiteral("parity")};
            if (!pred.contains(op) && !seq.contains(op))
            {
                errs << QStringLiteral("%1 的校验算子未知：%2").arg(name, op);
                continue;
            }
            if (seq.contains(op))
            {
                const QJsonArray over = c.value(QStringLiteral("over")).toArray();
                if (over.isEmpty())
                    errs << QStringLiteral("%1 的 %2 缺少 over").arg(name, op);
                for (const QJsonValue &ov : over)
                {
                    const QString r = ov.toString();
                    const QStringList parts = r.split(QLatin1Char(':'));
                    if (parts.size() > 2 || parts.first().trimmed().isEmpty() ||
                        (parts.size() == 2 && parts.last().trimmed().isEmpty()))
                    {
                        errs << QStringLiteral("%1 的 over 区间非法：%2").arg(name, r);
                        continue;
                    }
                    for (const QString &p : parts)
                    {
                        if (!names.contains(p.trimmed()))
                            errs << QStringLiteral("%1 的 over 引用了未知段：%2").arg(name, p);
                    }
                    if (parts.size() == 2)
                    {
                        const int a = order.value(parts[0].trimmed(), -1);
                        const int b = order.value(parts[1].trimmed(), -1);
                        if (a >= 0 && b >= 0 && a > b)
                            errs << QStringLiteral("%1 的 over 区间顺序反了：%2").arg(name, r);
                    }
                }
                const int need = operatorWidth(op);
                int elem = 0;
                if (literalSize(segCount(seg), &elem))
                {
                    const int have = elem * typeBytes(type);
                    if (need > 0 && have != need)
                        errs << QStringLiteral("%1 的字段宽度 %2B 不匹配 %3 输出 %4B")
                                    .arg(name).arg(have).arg(op).arg(need);
                }
            }
            else if (op == QLatin1String("in"))
            {
                if (c.value(QStringLiteral("values")).toArray().isEmpty())
                    errs << QStringLiteral("%1 的 in 缺少 values").arg(name);
            }
            else if (op == QLatin1String("range"))
            {
                if (!c.contains(QStringLiteral("min")) || !c.contains(QStringLiteral("max")))
                    errs << QStringLiteral("%1 的 range 缺少 min/max").arg(name);
            }
            else if (op == QLatin1String("mask"))
            {
                if (!c.contains(QStringLiteral("and")) || !c.contains(QStringLiteral("eq")))
                    errs << QStringLiteral("%1 的 mask 缺少 and/eq").arg(name);
            }
            else if (!c.contains(QStringLiteral("value")) && !c.contains(QStringLiteral("expr")))
                errs << QStringLiteral("%1 的 %2 缺少比较值").arg(name, op);
        }
    }

    const QJsonObject ts = o.value(QStringLiteral("timestamp")).toObject();
    if (!ts.isEmpty())
    {
        if (ts.value(QStringLiteral("expr")).toString().trimmed().isEmpty())
            errs << QStringLiteral("timestamp.expr 不能为空");
        const QString unit = ts.value(QStringLiteral("unit")).toString(QStringLiteral("s"));
        if (unit != QLatin1String("s") && unit != QLatin1String("ms") && unit != QLatin1String("us"))
            errs << QStringLiteral("timestamp.unit 只能是 s/ms/us");
    }

    const QJsonObject series = o.value(QStringLiteral("series")).toObject();
    if (!series.isEmpty())
    {
        const QString selector = series.value(QStringLiteral("selector")).toString().trimmed();
        if (selector.isEmpty())
            errs << QStringLiteral("series.selector 不能为空");
        const QString payload = series.value(QStringLiteral("payload")).toString().trimmed();
        if (payload.isEmpty())
            errs << QStringLiteral("series.payload 不能为空");
        else if (!names.contains(payload))
            errs << QStringLiteral("series.payload 引用了未知段：%1").arg(payload);
        else if (roles.value(QLatin1String("payload")) != payload)
            errs << QStringLiteral("series.payload 必须指向 role=payload 的段");
        const QString fallback = series.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"));
        if (fallback != QLatin1String("reject") && fallback != QLatin1String("raw"))
            errs << QStringLiteral("series.fallback 只能是 reject 或 raw");
        const QJsonArray vars = series.value(QStringLiteral("variants")).toArray();
        if (vars.isEmpty())
            errs << QStringLiteral("series 至少需要一个变体");
        QSet<QString> varNames;
        QSet<QString> varValues;
        int valueCount = 0;
        auto addVariantKey = [&](const QJsonValue &x) {
            varValues.insert(x.isString() ? QStringLiteral("s:") + x.toString()
                                           : QStringLiteral("n:") + QString::number(x.toDouble(), 'g', 17));
            ++valueCount;
        };
        int payloadBytes = -1;
        if (!payload.isEmpty())
            payloadBytes = fixedBytes.value(payload, -1);
        for (const QJsonValue &vv : vars)
        {
            const QJsonObject var = vv.toObject();
            const QString vn = var.value(QStringLiteral("name")).toString().trimmed();
            if (vn.isEmpty())
                errs << QStringLiteral("变体缺少 name");
            else if (varNames.contains(vn))
                errs << QStringLiteral("变体名重复：%1").arg(vn);
            varNames.insert(vn);
            if (var.contains(QStringLiteral("values")))
            {
                for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                    addVariantKey(x);
            }
            else if (var.contains(QStringLiteral("value")))
            {
                addVariantKey(var.value(QStringLiteral("value")));
            }
            else
                errs << QStringLiteral("变体 %1 缺少 value/values").arg(vn);
            const QJsonArray vsegs = var.value(QStringLiteral("segments")).toArray();
            int vbytes = 0;
            bool vvariable = false;
            for (const QJsonValue &vsv : vsegs)
            {
                const QJsonObject vseg = vsv.toObject();
                int n = 0;
                if (!literalSize(segCount(vseg), &n))
                {
                    vvariable = true;
                    continue;
                }
                vbytes += n * typeBytes(vseg.value(QStringLiteral("type")).toString());
            }
            if (payloadBytes >= 0 && !vvariable && vbytes != payloadBytes)
                errs << QStringLiteral("变体 %1 的段总字节 %2 与 payload %3 不一致")
                            .arg(vn).arg(vbytes).arg(payloadBytes);
        }
        if (valueCount > 0 && varValues.size() != valueCount)
            errs << QStringLiteral("变体 value/values 有重复");
    }

    int fixedTotal = 0;
    bool hasVariable = false;
    for (int b : fixedBytes)
        fixedTotal += b;
    for (const QJsonValue &sv : segArr)
        if (!segCount(sv.toObject()).isDouble())
            hasVariable = true;
    if (!hasVariable && fixedTotal > maxsize)
        errs << QStringLiteral("固定段总字节 %1 超过 maxsize %2").arg(fixedTotal).arg(maxsize);
    return errs;
}


} // namespace nextpilot::plotscope::protocol
