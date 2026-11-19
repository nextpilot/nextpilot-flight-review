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
            QStringLiteral("bcd"), QStringLiteral("char")};
}

bool parseType(const std::string &raw, TypeInfo &ti, std::string &err)
{
    std::string t = raw;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    if (t == "u8") { ti = {ValueType::U8, 1}; return true; }
    if (t == "i8") { ti = {ValueType::I8, 1}; return true; }
    if (t == "u16") { ti = {ValueType::U16, 2}; return true; }
    if (t == "i16") { ti = {ValueType::I16, 2}; return true; }
    if (t == "u32") { ti = {ValueType::U32, 4}; return true; }
    if (t == "i32") { ti = {ValueType::I32, 4}; return true; }
    if (t == "u64") { ti = {ValueType::U64, 8}; return true; }
    if (t == "i64") { ti = {ValueType::I64, 8}; return true; }
    if (t == "f32") { ti = {ValueType::F32, 4}; return true; }
    if (t == "f64") { ti = {ValueType::F64, 8}; return true; }
    if (t == "bool") { ti = {ValueType::Bool, 1}; return true; }
    if (t == "bcd") { ti = {ValueType::Bcd, 1}; return true; }
    if (t == "char") { ti = {ValueType::Text, 1}; return true; }
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

namespace
{

// 段里出现任何 v1 键都直接报错：v2 是唯一方言，不做静默兼容。
void checkNoV1Keys(const QJsonObject &seg, const QString &name, QStringList &errs)
{
    static const char *kForbidden[] = {"size", "counts", "value", "hidden", "plot", "role", "hasValue"};
    for (const char *k : kForbidden)
    {
        if (!seg.contains(QLatin1String(k)))
            continue;
        const QString key = QString::fromLatin1(k);
        if (key == QLatin1String("value") && seg.value(key).isUndefined())
            continue;
        errs << QStringLiteral("%1 使用了已废弃的 v1 键 %2（v2 用 count/length/expect/emit）")
                    .arg(name, key);
    }
}

// 校验 emit 取值。
void checkEmit(const QJsonObject &seg, const QString &name, QStringList &errs)
{
    if (!seg.contains(QStringLiteral("emit")))
        return;
    const QString e = seg.value(QStringLiteral("emit")).toString();
    if (e != QLatin1String("channel") && e != QLatin1String("aux") && e != QLatin1String("none"))
        errs << QStringLiteral("%1 的 emit 只能是 channel/aux/none：%2").arg(name, e);
}

} // namespace

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

    // series.payload 的段名：它是变体容器，允许被 length.fields 引用。
    const QString payloadName =
        o.value(QStringLiteral("series")).toObject().value(QStringLiteral("payload")).toString().trimmed();
    QSet<QString> names;
    QMap<QString, int> order;
    QMap<QString, int> fixedBytes;
    QSet<QString> lengthNames;
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

        checkNoV1Keys(seg, name, errs);
        checkEmit(seg, name, errs);

        int n = 0;
        const QJsonValue count = seg.value(QStringLiteral("count"));
        if (literalSize(count, &n))
            fixedBytes.insert(name, n * typeBytes(seg.value(QStringLiteral("type")).toString()));
        else if (!count.isString() || count.toString().trimmed().isEmpty())
            errs << QStringLiteral("%1 的 count 必须是正整数或表达式").arg(name);
        const QString type = seg.value(QStringLiteral("type")).toString().toLower();
        if (!typeNames().contains(type))
            errs << QStringLiteral("%1 的类型未知：%2").arg(name, type);

        // 位域（SPEC §6）：宽度 <= 64，且 width + offset%8 <= 64，单次 loadBytes<=8 即可取完。
        if (seg.contains(QStringLiteral("bit")))
        {
            const QJsonValue bv = seg.value(QStringLiteral("bit"));
            if (!bv.isObject())
            {
                errs << QStringLiteral("%1 的 bit 必须是对象").arg(name);
            }
            else
            {
                const QJsonObject bo = bv.toObject();
                const int width = bo.value(QStringLiteral("width")).toInt(0);
                const double offset = bo.value(QStringLiteral("offset")).toDouble(0);
                const QString border = bo.value(QStringLiteral("order")).toString(QStringLiteral("lsb0"));
                if (width <= 0 || width > 64)
                    errs << QStringLiteral("%1 的 bit.width 必须在 1..64").arg(name);
                if (offset < 0)
                    errs << QStringLiteral("%1 的 bit.offset 不能为负").arg(name);
                const int rem = static_cast<int>(static_cast<long long>(offset) % 8);
                if (width + rem > 64)
                    errs << QStringLiteral("%1 的 bit 跨度过大：width + offset%8 <= 64").arg(name);
                if (border != QLatin1String("lsb0") && border != QLatin1String("msb0"))
                    errs << QStringLiteral("%1 的 bit.order 只能是 lsb0/msb0").arg(name);
                if (!type.startsWith(QLatin1Char('u')) && !type.startsWith(QLatin1Char('i')))
                    errs << QStringLiteral("%1 的位域必须是整型").arg(name);
                const QString base = bo.value(QStringLiteral("base")).toString().trimmed();
                if (!base.isEmpty())
                {
                    // base 必须是本段之前出现的段，且不是位域段（否则位视图套位视图）
                    auto oit = order.find(base);
                    if (oit == order.end())
                        errs << QStringLiteral("%1 的 bit.base 引用了未知段：%2").arg(name, base);
                    else if (oit.value() >= order.value(name, 0))
                        errs << QStringLiteral("%1 的 bit.base 必须在前序：%2").arg(name, base);
                }
            }
        }

        // 条件段：if 为表达式，条件不成立时该段不占字节、不产通道。
        if (seg.contains(QStringLiteral("if")))
        {
            const QJsonValue iv = seg.value(QStringLiteral("if"));
            if (iv.isString() && iv.toString().trimmed().isEmpty())
                errs << QStringLiteral("%1 的 if 不能为空").arg(name);
        }

        // length 段：自声明为长度，必须单元素整型。
        if (seg.contains(QStringLiteral("length")))
        {
            lengthNames.insert(name);
            if (count.isDouble() && count.toDouble() > 1)
                errs << QStringLiteral("length 段 %1 应为单元素").arg(name);
            if (!type.startsWith(QLatin1Char('u')) && !type.startsWith(QLatin1Char('i')))
                errs << QStringLiteral("length 段 %1 必须是整型").arg(name);
            const QJsonValue lv = seg.value(QStringLiteral("length"));
            if (lv.isObject())
            {
                const QJsonObject lo = lv.toObject();
                if (!lo.contains(QStringLiteral("fields")) && !lo.contains(QStringLiteral("expr")))
                    errs << QStringLiteral("%1 的 length 需要 fields 或 expr").arg(name);
                const QString unit = lo.value(QStringLiteral("unit")).toString(QStringLiteral("elements"));
                if (unit != QLatin1String("bytes") && unit != QLatin1String("elements"))
                    errs << QStringLiteral("%1 的 length.unit 只能是 bytes/elements").arg(name);
                for (const QJsonValue &fv : lo.value(QStringLiteral("fields")).toArray())
                {
                    const QString fn = fv.toString().trimmed();
                    // payload 是 series 的容器段，长度按变体内容反算，允许被 length 引用。
                    if (!fn.isEmpty() && !names.contains(fn) && fn != payloadName)
                        errs << QStringLiteral("%1 的 length.fields 引用了未知段：%2").arg(name, fn);
                }
            }
            else if (!lv.isString() || lv.toString().trimmed().isEmpty())
                errs << QStringLiteral("%1 的 length 必须是对象或表达式").arg(name);
        }

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
                if (literalSize(count, &elem))
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
        if (unit != QLatin1String("s") && unit != QLatin1String("ms") &&
            unit != QLatin1String("us") && unit != QLatin1String("ns"))
            errs << QStringLiteral("timestamp.unit 只能是 s/ms/us/ns");
        const QString origin = ts.value(QStringLiteral("origin")).toString(QStringLiteral("first_frame"));
        if (origin != QLatin1String("first_frame") && origin != QLatin1String("absolute"))
            errs << QStringLiteral("timestamp.origin 只能是 first_frame/absolute");
        const QString fb = ts.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"));
        if (fb != QLatin1String("reject") && fb != QLatin1String("index"))
            errs << QStringLiteral("timestamp.fallback 只能是 reject/index");
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
        else
        {
            const QJsonObject po = segArr.at(order.value(payload, 0)).toObject();
            if (po.value(QStringLiteral("type")).toString().toLower() != QLatin1String("u8"))
                errs << QStringLiteral("series.payload 段 %1 必须是 u8").arg(payload);
            // payload 是变体容器，宽度可以是常量也可以是长度表达式；
            // 变体段必须精确填满它，这条由解码期校验（静态期不猜）。
        }
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
                const QString vname = vseg.value(QStringLiteral("name")).toString();
                checkNoV1Keys(vseg, vname.isEmpty() ? QStringLiteral("变体段") : vname, errs);
                checkEmit(vseg, vname.isEmpty() ? QStringLiteral("变体段") : vname, errs);
                int n = 0;
                if (!literalSize(vseg.value(QStringLiteral("count")), &n))
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
        if (!sv.toObject().value(QStringLiteral("count")).isDouble())
            hasVariable = true;
    if (!hasVariable && fixedTotal > maxsize)
        errs << QStringLiteral("固定段总字节 %1 超过 maxsize %2").arg(fixedTotal).arg(maxsize);
    return errs;
}

} // namespace nextpilot::plotscope::protocol
