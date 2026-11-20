#include "protocol/ProtocolSpec.h"
#include "protocol/DigestEngine.h"

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
            QStringLiteral("bcd"), QStringLiteral("char"),
            // text 文本协议的类型名（SPEC §7）：char/string/rest 都是文本，number 是数值。
            QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("rest"),
            QStringLiteral("time")};
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
    // text 文本协议的三个类型名（SPEC §7）。文本没有字节宽概念，size 只用于"每元素 1 字节"的类比。
    if (t == "string" || t == "str") { ti = {ValueType::Text, 1}; return true; }
    if (t == "number" || t == "num") { ti = {ValueType::F64, 8}; return true; }
    if (t == "rest") { ti = {ValueType::Text, 1}; return true; }
    // 文本时间：hhmmss / hhmmss.ss / hh:mm:ss[.ss] → 当天秒（f64）。只用于 text 段。
    if (t == "time") { ti = {ValueType::F64, 8, true}; return true; }
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

// 通用段键校验（SPEC §2.1）——bin 与 text 两种形态同口径执行，
// 此前这几项只在 checkTextSegment 里校验，bin 段写错类型会被静默放行。
void checkCommonSegment(const QJsonObject &seg, const QString &name,
                        const QMap<QString, int> &order, const QSet<QString> &names,
                        int selfIndex, bool textSeg, QStringList &errs)
{
    // 类型硬校验：JSON 里把键写成错误类型不能被静默当默认值（例如 scale:null → 0）。
    if (seg.contains(QStringLiteral("unit")) && !seg.value(QStringLiteral("unit")).isString())
        errs << QStringLiteral("%1 的 unit 必须是字符串").arg(name);
    if (seg.contains(QStringLiteral("prefix")) && !seg.value(QStringLiteral("prefix")).isString())
        errs << QStringLiteral("%1 的 prefix 必须是字符串").arg(name);
    if (seg.contains(QStringLiteral("scale")) && !seg.value(QStringLiteral("scale")).isDouble())
        errs << QStringLiteral("%1 的 scale 必须是数值").arg(name);
    if (seg.contains(QStringLiteral("offset")) && !seg.value(QStringLiteral("offset")).isDouble())
        errs << QStringLiteral("%1 的 offset 必须是数值").arg(name);
    if (seg.contains(QStringLiteral("enum")))
    {
        const QJsonValue ev = seg.value(QStringLiteral("enum"));
        if (!ev.isArray() || ev.toArray().isEmpty())
            errs << QStringLiteral("%1 的 enum 必须是非空数值数组").arg(name);
        else
            for (const QJsonValue &v : ev.toArray())
                if (!v.isDouble())
                    errs << QStringLiteral("%1 的 enum 元素必须是数值").arg(name);
    }
    if (seg.contains(QStringLiteral("range")))
    {
        const QJsonValue rv = seg.value(QStringLiteral("range"));
        if (rv.isArray())
        {
            if (rv.toArray().isEmpty() || rv.toArray().size() > 2)
                errs << QStringLiteral("%1 的 range 必须是 [min] 或 [min,max]").arg(name);
            for (const QJsonValue &v : rv.toArray())
                if (!v.isDouble())
                    errs << QStringLiteral("%1 的 range 元素必须是数值").arg(name);
        }
        else if (rv.isObject())
        {
            if (!rv.toObject().value(QStringLiteral("min")).isDouble() ||
                !rv.toObject().value(QStringLiteral("max")).isDouble())
                errs << QStringLiteral("%1 的 range 需要数值 min/max").arg(name);
        }
        else
            errs << QStringLiteral("%1 的 range 必须是 [min,max] 或 {min,max}").arg(name);
    }
    // expect 是 v3 的约束数组：bin 段由 validateBin 校验，text 段等独立解析器补齐。
    if (seg.contains(QStringLiteral("on_mismatch")))
    {
        const QString m = seg.value(QStringLiteral("on_mismatch")).toString();
        if (m != QLatin1String("reject") && m != QLatin1String("warn") && m != QLatin1String("skip"))
            errs << QStringLiteral("%1 的 on_mismatch 只能是 reject/warn/skip：%2").arg(name, m);
    }
    if (textSeg && seg.contains(QStringLiteral("pattern")))
    {
        const QString p = seg.value(QStringLiteral("pattern")).toString();
        if (p.isEmpty())
            errs << QStringLiteral("%1 的 pattern 为空").arg(name);
        else
        {
            QRegularExpression re(p);
            if (!re.isValid())
                errs << QStringLiteral("%1 的 pattern 非法：%2").arg(name, re.errorString());
        }
    }
    if (seg.contains(QStringLiteral("transform")))
    {
        const QJsonValue tv = seg.value(QStringLiteral("transform"));
        if (tv.isArray())
        {
            for (const QJsonValue &v : tv.toArray())
            {
                const QString op = v.isString() ? v.toString()
                                                : v.toObject().value(QStringLiteral("op")).toString();
                static const QStringList kOps = {QStringLiteral("sexagesimal"), QStringLiteral("scale"),
                                                  QStringLiteral("mul"), QStringLiteral("offset"),
                                                  QStringLiteral("add"), QStringLiteral("abs"),
                                                  QStringLiteral("round"), QStringLiteral("clamp")};
                if (!kOps.contains(op))
                    errs << QStringLiteral("%1 的 transform 不支持 op=%2").arg(name, op);
                if (op == QLatin1String("clamp") &&
                    v.toObject().value(QStringLiteral("min")).toDouble(0) >
                        v.toObject().value(QStringLiteral("max")).toDouble(0))
                    errs << QStringLiteral("%1 的 transform.clamp min > max").arg(name);
                if (!v.isString())
                {
                    const int split = v.toObject().value(QStringLiteral("split")).toInt(2);
                    if (split < 0 || split > 8)
                        errs << QStringLiteral("%1 的 transform.split 必须在 0..8").arg(name);
                }
            }
        }
    }

    if (seg.contains(QStringLiteral("digest")))
    {
        const QJsonValue dv = seg.value(QStringLiteral("digest"));
        if (!dv.isObject())
        {
            errs << QStringLiteral("%1 的 digest 必须是对象").arg(name);
            return;
        }
        const QJsonObject dg = dv.toObject();
        // 白名单必须与 psproto.schema.json 的 digest 定义一致，漏一个就会误拒合法模板。
        static const QStringList kDigestKeys = {QStringLiteral("alg"), QStringLiteral("code"),
                                                QStringLiteral("from"), QStringLiteral("to"),
                                                QStringLiteral("width"), QStringLiteral("poly"),
                                                QStringLiteral("init"), QStringLiteral("xorout"),
                                                QStringLiteral("refin"), QStringLiteral("refout"),
                                                QStringLiteral("inject")};
        for (const QString &k : dg.keys())
            if (!kDigestKeys.contains(k))
                errs << QStringLiteral("%1 的 digest 未知键：%2（端点用 from/to，不用 over）").arg(name, k);
        const QString alg = dg.value(QStringLiteral("alg")).toString();
        const QString code = dg.value(QStringLiteral("code")).toString();
        if (alg.isEmpty() && code.isEmpty())
            errs << QStringLiteral("%1 的 digest 需要 alg 或 code").arg(name);
        if (!alg.isEmpty() && !code.isEmpty())
            errs << QStringLiteral("%1 的 digest 不能同时给 alg 和 code").arg(name);
        if (!alg.isEmpty() && !digestAlgKnown(alg.toStdString()))
            errs << QStringLiteral("%1 的摘要算子未知：%2（可用：%3）")
                        .arg(name, alg, digestAlgorithmNames().join(QLatin1Char('/')));
        if (!code.isEmpty() && !scriptDigestAvailable())
            errs << QStringLiteral("%1 的 digest 用了脚本，但脚本后端未启用").arg(name);

        auto checkEndpoint = [&](const QString &key, const QString &val, bool isFrom) {
            if (val.isEmpty())
                return;
            if (val == QLatin1String("frame_start") || val == QLatin1String("frame_end"))
            {
                if (isFrom && val == QLatin1String("frame_end"))
                    errs << QStringLiteral("%1 的 digest.from 不能用 frame_end").arg(name);
                if (!isFrom && val == QLatin1String("frame_start"))
                    errs << QStringLiteral("%1 的 digest.to 不能用 frame_start").arg(name);
                return;
            }
            const QString prefix = isFrom ? QStringLiteral("after:") : QStringLiteral("before:");
            if (!val.startsWith(prefix))
            {
                errs << QStringLiteral("%1 的 digest.%2 只能是 %3<段名>/%4")
                            .arg(name, isFrom ? QStringLiteral("from") : QStringLiteral("to"), prefix,
                                 isFrom ? QStringLiteral("frame_start") : QStringLiteral("frame_end"));
                return;
            }
            const QString ref = val.mid(prefix.size()).trimmed();
            if (!names.contains(ref))
            {
                errs << QStringLiteral("%1 的 digest 引用了未知段：%2").arg(name, ref);
                return;
            }
            const int idx = order.value(ref, -1);
            if (idx < 0 || idx >= selfIndex)
                errs << QStringLiteral("%1 的 digest 区间跨越了校验段自身（%2 必须在前序）").arg(name, ref);
        };
        checkEndpoint(QStringLiteral("from"), dg.value(QStringLiteral("from")).toString(), true);
        checkEndpoint(QStringLiteral("to"), dg.value(QStringLiteral("to")).toString(), false);
    }
}

// text 段校验（SPEC §7）：边界来自 literal/delimiter/width，不再有字节宽度语义。
// digest 端点用段序表校验引用（避免"校验一个还没解析的段"）。
void checkTextSegment(const QJsonObject &seg, const QString &name, const QMap<QString, int> &order,
                       const QSet<QString> &names, int selfIndex, QStringList &errs)
{
    // v3：text 段也允许序列校验（NMEA 的 XOR 就是按字节区间算的），不再区分 digest/checks。
    const QString lit = seg.value(QStringLiteral("literal")).toString();
    if (seg.contains(QStringLiteral("key")))
    {
        const QString k = seg.value(QStringLiteral("key")).toString();
        if (k.isEmpty())
            errs << QStringLiteral("%1：key 不能为空").arg(name);
        else if (k.contains(QLatin1Char('=')) || k.contains(QLatin1Char(',')))
            errs << QStringLiteral("%1：key 不能含 = 或 ,：%2").arg(name).arg(k);
        if (!lit.isEmpty())
            errs << QStringLiteral("%1：key 与 literal 互斥").arg(name);
    }
    const bool hasDelim = seg.contains(QStringLiteral("delimiter"));
    const bool hasWidth = seg.contains(QStringLiteral("width"));
    const bool isRest = seg.value(QStringLiteral("rest")).toBool(false);

    if (!lit.isEmpty() && (hasDelim || hasWidth))
        errs << QStringLiteral("%1 是字面量段，不能再声明 delimiter/width（它只做精确匹配）").arg(name);
    if (isRest && hasDelim)
        errs << QStringLiteral("%1 声明了 rest（整块原文），不能再按 delimiter 切").arg(name);

    if (seg.contains(QStringLiteral("repeater")))
    {
        const QString r = seg.value(QStringLiteral("repeater")).toString();
        if (r != QLatin1String("*") && !r.isEmpty())
            errs << QStringLiteral("%1 的 repeater 只能是 \"*\"（单元素省略即可）：%2").arg(name, r);
    }

    if (hasWidth)
    {
        const int w = seg.value(QStringLiteral("width")).toInt(0);
        if (w <= 0 || w > 65535)
            errs << QStringLiteral("%1 的 width 必须在 1..65535").arg(name);
    }

    if (hasDelim)
    {
        QStringList list;
        const QJsonValue dv = seg.value(QStringLiteral("delimiter"));
        if (dv.isArray())
        {
            for (const QJsonValue &v : dv.toArray())
                list << v.toString();
        }
        else
            list << dv.toString();
        if (list.isEmpty())
            errs << QStringLiteral("%1 的 delimiter 为空").arg(name);
        for (const QString &d : list)
        {
            if (d.isEmpty())
                errs << QStringLiteral("%1 的 delimiter 含空串").arg(name);
        }
        if (list.size() > 1 && list.contains(QStringLiteral("blank")))
            errs << QStringLiteral("%1 的 blank 不能与其他分隔符混用").arg(name);
        if (list.contains(QStringLiteral("blank")) && list.size() > 1)
            errs << QStringLiteral("%1 的 delimiter 重复").arg(name);
    }

    if (seg.contains(QStringLiteral("base")))
    {
        const double b = seg.value(QStringLiteral("base")).toDouble(10);
        if (b != 2 && b != 8 && b != 10 && b != 16)
            errs << QStringLiteral("%1 的 base 只能是 2/8/10/16").arg(name);
    }
    if (seg.contains(QStringLiteral("fixed")))
    {
        const int f = seg.value(QStringLiteral("fixed")).toInt(-1);
        if (f < 0 || f > 18)
            errs << QStringLiteral("%1 的 fixed（小数位数）必须在 0..18").arg(name);
    }
    if (seg.contains(QStringLiteral("empty")))
    {
        const QString e = seg.value(QStringLiteral("empty")).toString();
        if (!e.contains(QLatin1String("nan")) && e != QLatin1String("skip") && e != QLatin1String("raw"))
            errs << QStringLiteral("%1 的 empty 只能是 nan/skip/raw：%2").arg(name, e);
    }
    checkCommonSegment(seg, name, order, names, selfIndex, true, errs);
}

QStringList validate(const QJsonObject &o)
{
    // 形态分派只发生在这里：之后 bin/text 各自独立校验，不再传 bool 往下走。
    if (o.value(QStringLiteral("mode")).toString() == QLatin1String("bin"))
        return validateBin(o);

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
    {
        const QString fmt = o.value(QStringLiteral("mode")).toString();
        if (fmt.isEmpty())
            errs << QStringLiteral("顶层 mode 必填（bin / text，无 mixed）");
        else if (fmt != QLatin1String("bin") && fmt != QLatin1String("text"))
            errs << QStringLiteral("mode 只能是 bin 或 text");
    }
    if (o.contains(QStringLiteral("maxsize")) && !o.value(QStringLiteral("maxsize")).isDouble())
        errs << QStringLiteral("maxsize 必须是数值");
    const int maxsize = o.value(QStringLiteral("maxsize")).toInt(4096);
    if (maxsize < 2 || maxsize > 65535)
        errs << QStringLiteral("maxsize 必须在 2..65535");
    const QStringList aliasNames = o.value(QStringLiteral("types")).toObject()
                                       .keys();
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
    {
        // term 是候选集合：写了就硬校验，没写就不管（SPEC §7.2）。
        const QJsonValue tv = o.contains(QStringLiteral("term"))
                                  ? o.value(QStringLiteral("term"))
                                  : o.value(QStringLiteral("terminator"));
        if (!tv.isUndefined() && !tv.isNull())
        {
            QStringList list;
            if (tv.isArray())
            {
                for (const QJsonValue &v : tv.toArray())
                    list << v.toString();
            }
            else
                list << tv.toString();
            if (list.isEmpty())
                errs << QStringLiteral("term 不能为空数组");
            for (const QString &d : list)
                if (d.isEmpty())
                    errs << QStringLiteral("term 含空串");
        }
    }

    // series.payload 的段名：它是变体容器，允许被 length.fields 引用。
    const QString payloadName =
        o.value(QStringLiteral("series")).toObject().value(QStringLiteral("payload")).toString().trimmed();
    // 形态由顶层 mode 显式声明，不靠段特征推断（SPEC §2.1.1）。
    const bool docText = (o.value(QStringLiteral("mode")).toString() == QLatin1String("text"));

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

        const bool isDerived = seg.contains(QStringLiteral("derive"));
        if (isDerived)
        {
            static const char *kReadKeys[] = {"type", "count", "bit",    "literal",  "delimiter",
                                              "width", "rest", "key",   "repeater", "digest",
                                              "expect", "endian"};
            for (const char *k : kReadKeys)
                if (seg.contains(QLatin1String(k)))
                    errs << QStringLiteral("%1 是 derive 段（不读数据），不能声明 %2").arg(name, QString::fromLatin1(k));
        }

        const bool textSeg = docText; // 不设 mixed：整份协议同一种形态

        // 键黑白名单（SPEC §2.1.1）：bin 段禁文本词法键，text 段禁字节/位域键。
        if (docText)
        {
            static const char *binOnly[] = {"bit", "repeat", "until", "length", "endian"};
            for (const char *k : binOnly)
                if (seg.contains(QLatin1String(k)))
                    errs << QStringLiteral("%1 是 text 段，不能声明 %2（字节/位域键归 bin）").arg(name, QString::fromLatin1(k));
        }
        else
        {
            static const char *textOnly[] = {"literal", "key", "delimiter", "repeater", "width",
                                             "rest", "pattern", "empty", "base", "fixed", "signed", "sci"};
            for (const char *k : textOnly)
                if (seg.contains(QLatin1String(k)))
                    errs << QStringLiteral("%1 是 bin 段，不能声明 %2（文本词法键归 text）").arg(name, QString::fromLatin1(k));
        }
        if (seg.contains(QStringLiteral("repeat")))
        {
            const QJsonValue rv = seg.value(QStringLiteral("repeat"));
            const QString mode = rv.isString() ? rv.toString()
                                               : rv.toObject().value(QStringLiteral("mode")).toString();
            if (mode != QLatin1String("eos") && mode != QLatin1String("until"))
                errs << QStringLiteral("%1 的 repeat.mode 只能是 eos/until：%2").arg(name, mode);
            if (mode == QLatin1String("until") &&
                rv.toObject().value(QStringLiteral("until")).toString().isEmpty())
                errs << QStringLiteral("%1 的 repeat.until 不能为空").arg(name);
            if (textSeg)
                errs << QStringLiteral("%1：text 段用 repeater:\"*\"，不要用 repeat").arg(name);
        }

        int n = 0;
        const QJsonValue count = seg.value(QStringLiteral("count"));
        if (isDerived)
        {
            // 派生字段不读数据：count/字节宽度都不适用
        }
        else if (textSeg)
        {
            // text 段没有"元素数 × 字节宽"语义：count 缺省 1，repeater:"*" 表示重复到下一段边界。
            if (!count.isUndefined() && !literalSize(count, &n))
                errs << QStringLiteral("%1 的 count 必须是正整数（text 段可省略）").arg(name);
            if (seg.contains(QStringLiteral("bit")))
                errs << QStringLiteral("%1 是 text 段，不能声明 bit（位域只对字节区间有意义）").arg(name);
            checkTextSegment(seg, name, order, names, order.value(name, 0), errs);
        }
        else if (count.isUndefined())
        {
            // 标量省略 count = 1（SPEC §2.1），字节数由 type 推算
            fixedBytes.insert(name, typeBytes(seg.value(QStringLiteral("type")).toString()));
        }
        else if (literalSize(count, &n))
            fixedBytes.insert(name, n * typeBytes(seg.value(QStringLiteral("type")).toString()));
        else if (!count.isString() || count.toString().trimmed().isEmpty())
            errs << QStringLiteral("%1 的 count 必须是正整数或表达式").arg(name);
        const QString type = seg.value(QStringLiteral("type")).toString().toLower();
        if (!isDerived && !typeNames().contains(type) && !aliasNames.contains(type))
            errs << QStringLiteral("%1 的类型未知：%2").arg(name, type);
        if (type == QLatin1String("time") && !textSeg)
            errs << QStringLiteral("%1：type:time 只用于 text 段（文本时间），二进制请用 u32/u64 + transform").arg(name);

        // bin 段同样要过通用键校验（on_mismatch/pattern/transform/digest），
        // 否则 bin 段写错类型只会被静默吞掉，运行期才发现。
        if (!textSeg && !isDerived)
            checkCommonSegment(seg, name, order, names, order.value(name, 0), false, errs);

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

    // repeat:"eos" 是"读到帧尾"，因此它必须是最后一个读字节的段：
    // 后面再出现读字节的段（非 bit），eos 会把它们一起吞掉。
    for (size_t i = 0; i < static_cast<size_t>(segArr.size()); ++i)
    {
        const QJsonObject so = segArr.at(static_cast<int>(i)).toObject();
        const QJsonValue rv = so.value(QStringLiteral("repeat"));
        const QString mode = rv.isString() ? rv.toString()
                                           : rv.toObject().value(QStringLiteral("mode")).toString();
        if (mode != QLatin1String("eos"))
            continue;
        for (size_t j = i + 1; j < static_cast<size_t>(segArr.size()); ++j)
        {
            const QJsonObject after = segArr.at(static_cast<int>(j)).toObject();
            if (after.contains(QStringLiteral("bit")))
                continue;
            errs << QStringLiteral("段 %1 用了 repeat:\"eos\"，后面不能再有读字节的段（%2）：eos 是\"读到帧尾\"")
                        .arg(so.value(QStringLiteral("name")).toString(),
                             after.value(QStringLiteral("name")).toString());
            break;
        }
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
