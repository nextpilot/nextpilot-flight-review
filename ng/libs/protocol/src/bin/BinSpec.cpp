#include "protocol/ProtocolSpec.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::protocol
{

namespace
{

// bin 段允许的 10 个键。未知键即错误：v3 不做旧格式兼容。
const QStringList &binSegKeys()
{
    static const QStringList kKeys = {QStringLiteral("name"),       QStringLiteral("description"),
                                      QStringLiteral("unit"),       QStringLiteral("output"),
                                      QStringLiteral("type"),       QStringLiteral("count"),
                                      QStringLiteral("transform"),  QStringLiteral("expect"),
                                      QStringLiteral("on_mismatch"), QStringLiteral("if")};
    return kKeys;
}

const QStringList &binTopKeys()
{
    static const QStringList kKeys = {QStringLiteral("name"),        QStringLiteral("mode"),
                                      QStringLiteral("description"), QStringLiteral("layout"),
                                      QStringLiteral("endian"),      QStringLiteral("maxsize"),
                                      QStringLiteral("terminator"),  QStringLiteral("segments"),
                                      QStringLiteral("blocks"),      QStringLiteral("derived"),
                                      QStringLiteral("series"),      QStringLiteral("types"),
                                      QStringLiteral("timestamp"),   QStringLiteral("term")};
    return kKeys;
}

const QStringList &transformOps()
{
    static const QStringList kOps = {QStringLiteral("sexagesimal"), QStringLiteral("scale"),
                                     QStringLiteral("mul"),         QStringLiteral("offset"),
                                     QStringLiteral("add"),         QStringLiteral("abs"),
                                     QStringLiteral("round"),       QStringLiteral("clamp"),
                                     QStringLiteral("sin"),         QStringLiteral("cos"),
                                     QStringLiteral("tan"),         QStringLiteral("sqrt"),
                                     QStringLiteral("log"),         QStringLiteral("exp")};
    return kOps;
}

const QStringList &predicateOps()
{
    static const QStringList kOps = {QStringLiteral("eq"), QStringLiteral("ne"), QStringLiteral("lt"),
                                     QStringLiteral("le"), QStringLiteral("gt"), QStringLiteral("ge"),
                                     QStringLiteral("in"), QStringLiteral("mask")};
    return kOps;
}

const QStringList &sequenceOps()
{
    static const QStringList kOps = {QStringLiteral("xor8"),       QStringLiteral("sum8"),
                                     QStringLiteral("crc8"),       QStringLiteral("crc16"),
                                     QStringLiteral("crc16_modbus"), QStringLiteral("crc16_x25"),
                                     QStringLiteral("crc24q"),     QStringLiteral("crc32"),
                                     QStringLiteral("crc32c"),     QStringLiteral("nmea_xor"),
                                     QStringLiteral("parity"),     QStringLiteral("script")};
    return kOps;
}

bool isIntegerType(const QString &t)
{
    return t.startsWith(QLatin1Char('u')) || t.startsWith(QLatin1Char('i'));
}

// count 的 "宽@偏移" 形式（bit 类型专用）。off 允许省略偏移写法之外的取值。
bool parseBitSpan(const QString &text, int *width, int *offset)
{
    const QStringList parts = text.split(QLatin1Char('@'));
    if (parts.size() != 2)
        return false;
    bool okW = false;
    bool okO = false;
    const int w = parts.at(0).trimmed().toInt(&okW);
    const int o = parts.at(1).trimmed().toInt(&okO);
    if (!okW || !okO)
        return false;
    *width = w;
    *offset = o;
    return true;
}

// 位域区间：[offset, offset+width) 必须落在 0..64 且不跨 8 字节边界，单次 8 字节读取即可取完。
bool bitSpanOk(int width, int offset, QString *why)
{
    if (width <= 0 || width > 64)
    {
        *why = QStringLiteral("位宽必须在 1..64");
        return false;
    }
    if (offset < 0)
    {
        *why = QStringLiteral("位偏移不能为负");
        return false;
    }
    if (width + (offset % 8) > 64)
    {
        *why = QStringLiteral("位域跨度过大：width + offset%8 <= 64");
        return false;
    }
    return true;
}

void checkUnknownKeys(const QJsonObject &obj, const QStringList &allowed, const QString &owner,
                      QStringList &errs)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!allowed.contains(it.key()))
            errs << QStringLiteral("%1 有未知键：%2").arg(owner, it.key());
}

// JSON 里把键写成错误类型不能被静默当默认值（例如 unit:123 → ""）。
void expectString(const QJsonObject &o, const char *key, const QString &owner, QStringList &errs)
{
    const QString k(key);
    if (o.contains(k) && !o.value(k).isString())
        errs << QStringLiteral("%1 的 %2 必须是字符串").arg(owner, k);
}

void expectNumber(const QJsonObject &o, const char *key, const QString &owner, QStringList &errs)
{
    const QString k(key);
    if (o.contains(k) && !o.value(k).isDouble())
        errs << QStringLiteral("%1 的 %2 必须是数值").arg(owner, k);
}

void checkTransform(const QJsonValue &tv, const QString &owner, const SpecContext &ctx, int selfIndex,
                    QStringList &errs)
{
    if (tv.isUndefined())
        return;
    if (tv.isString())
    {
        checkExprRefs(tv.toString(), owner + QStringLiteral(".transform"), ctx, selfIndex, errs);
        return;
    }
    if (!tv.isArray())
    {
        errs << QStringLiteral("%1 的 transform 必须是表达式或算子数组").arg(owner);
        return;
    }
    for (const QJsonValue &step : tv.toArray())
    {
        if (!step.isObject())
        {
            errs << QStringLiteral("%1 的 transform 数组元素必须是 {op:…}").arg(owner);
            continue;
        }
        const QJsonObject so = step.toObject();
        const QString op = so.value(QStringLiteral("op")).toString();
        if (!transformOps().contains(op))
            errs << QStringLiteral("%1 的 transform 不支持 op=%2").arg(owner, op);
        for (auto it = so.begin(); it != so.end(); ++it)
            if (it.key() != QLatin1String("op") && it.key() != QLatin1String("split") &&
                it.key() != QLatin1String("k") && it.key() != QLatin1String("b") &&
                it.key() != QLatin1String("min") && it.key() != QLatin1String("max"))
                errs << QStringLiteral("%1 的 transform 步骤有未知参数：%2").arg(owner, it.key());
        if (op == QLatin1String("clamp") &&
            so.value(QStringLiteral("min")).toDouble(0) > so.value(QStringLiteral("max")).toDouble(0))
            errs << QStringLiteral("%1 的 transform.clamp min > max").arg(owner);
    }
}

void checkExpect(const QJsonValue &ev, const QString &owner, const SpecContext &ctx, int selfIndex,
                 int fixedBytes, QStringList &errs)
{
    if (ev.isUndefined())
        return;
    if (!ev.isArray())
    {
        errs << QStringLiteral("%1 的 expect 必须是数组（每项一条约束，项间 AND）").arg(owner);
        return;
    }
    for (const QJsonValue &item : ev.toArray())
    {
        if (!item.isObject())
        {
            errs << QStringLiteral("%1 的 expect 元素必须是 {op:…} 对象").arg(owner);
            continue;
        }
        const QJsonObject c = item.toObject();
        const QString op = c.value(QStringLiteral("op")).toString();
        if (op.isEmpty())
        {
            errs << QStringLiteral("%1 的 expect 缺少 op").arg(owner);
            continue;
        }
        for (auto it = c.begin(); it != c.end(); ++it)
        {
            const QString k = it.key();
            static const QStringList kAllowed = {
                QStringLiteral("op"),      QStringLiteral("value"),  QStringLiteral("expr"),
                QStringLiteral("values"),  QStringLiteral("labels"), QStringLiteral("mask"),
                QStringLiteral("over"),    QStringLiteral("poly"),   QStringLiteral("init"),
                QStringLiteral("refin"),   QStringLiteral("refout"), QStringLiteral("xorout"),
                QStringLiteral("width"),   QStringLiteral("inject"), QStringLiteral("code")};
            if (!kAllowed.contains(k))
                errs << QStringLiteral("%1 的 expect 项有未知参数：%2").arg(owner, k);
        }

        if (predicateOps().contains(op))
        {
            if (op == QLatin1String("in"))
            {
                const QJsonArray vals = c.value(QStringLiteral("values")).toArray();
                if (vals.isEmpty())
                    errs << QStringLiteral("%1 的 in 缺少 values").arg(owner);
                if (c.contains(QStringLiteral("labels")))
                {
                    const QJsonObject labels = c.value(QStringLiteral("labels")).toObject();
                    if (labels.isEmpty())
                        errs << QStringLiteral("%1 的 in.labels 为空").arg(owner);
                }
            }
            else if (op == QLatin1String("mask"))
            {
                if (!c.contains(QStringLiteral("mask")))
                    errs << QStringLiteral("%1 的 mask 缺少 mask").arg(owner);
                if (!c.contains(QStringLiteral("value")))
                    errs << QStringLiteral("%1 的 mask 缺少 value（比对目标）").arg(owner);
            }
            else if (!c.contains(QStringLiteral("value")) && !c.contains(QStringLiteral("expr")))
                errs << QStringLiteral("%1 的 %2 缺少比较值").arg(owner, op);

            if (c.contains(QStringLiteral("expr")))
                checkExprRefs(c.value(QStringLiteral("expr")).toString(), owner + QStringLiteral(".expect"),
                              ctx, selfIndex, errs);
            continue;
        }

        if (!sequenceOps().contains(op))
        {
            errs << QStringLiteral("%1 的 expect 算子未知：%2").arg(owner, op);
            continue;
        }

        // 序列类：必须有覆盖区间，端点只能指向已定义的前序段。
        const QString over = c.value(QStringLiteral("over")).toString().trimmed();
        if (over.isEmpty())
        {
            errs << QStringLiteral("%1 的 %2 缺少 over（覆盖区间）").arg(owner, op);
        }
        else
        {
            QStringList parts = over.split(QLatin1Char(':'));
            if (over == QLatin1String("frame_start") || over == QLatin1String("frame_end"))
                parts = QStringList{over};
            if (parts.size() > 2 || parts.first().trimmed().isEmpty() ||
                (parts.size() == 2 && parts.last().trimmed().isEmpty()))
                errs << QStringLiteral("%1 的 over 区间非法：%2").arg(owner, over);
            for (const QString &p : parts)
            {
                const QString v = p.trimmed();
                if (v == QLatin1String("frame_start") || v == QLatin1String("frame_end"))
                    continue;
                if (!ctx.names.contains(v))
                    errs << QStringLiteral("%1 的 over 引用了未知段：%2").arg(owner, v);
            }
        }
        const int need = operatorWidth(op);
        if (need > 0 && fixedBytes > 0 && fixedBytes != need)
            errs << QStringLiteral("%1 的字段宽度 %2B 不匹配 %3 输出 %4B")
                        .arg(owner).arg(fixedBytes).arg(op).arg(need);
    }
}

} // namespace

QStringList validateBin(const QJsonObject &root)
{
    SpecContext ctx;
    QStringList &errs = ctx.errs;

    checkUnknownKeys(root, binTopKeys(), QStringLiteral("顶层"), errs);
    for (const char *k : {"name", "mode", "description", "layout", "endian", "terminator"})
        expectString(root, k, QStringLiteral("顶层"), errs);
    expectNumber(root, "maxsize", QStringLiteral("顶层"), errs);

    if (root.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        errs << QStringLiteral("协议 name 不能为空");
    const QString layout = root.value(QStringLiteral("layout")).toString(QStringLiteral("sequential"));
    if (layout != QLatin1String("sequential"))
        errs << QStringLiteral("layout 只能是 sequential");
    const QString endian = root.value(QStringLiteral("endian")).toString(QStringLiteral("little"));
    if (endian != QLatin1String("little") && endian != QLatin1String("big") && endian != QLatin1String("le") &&
        endian != QLatin1String("be"))
        errs << QStringLiteral("endian 只能是 little/big（别名 le/be）");
    if (root.contains(QStringLiteral("maxsize")) && !root.value(QStringLiteral("maxsize")).isDouble())
        errs << QStringLiteral("maxsize 必须是数值");
    ctx.maxsize = root.value(QStringLiteral("maxsize")).toInt(4096);
    if (ctx.maxsize < 2 || ctx.maxsize > 65535)
        errs << QStringLiteral("maxsize 必须在 2..65535");

    // types 别名先登记，段 type 可以引用
    QStringList aliasNames;
    {
        const QJsonObject types = root.value(QStringLiteral("types")).toObject();
        for (auto it = types.begin(); it != types.end(); ++it)
            aliasNames << it.key();
    }

    // blocks：先把块表建起来，段才能按名字引用
    const QJsonObject blocks = root.value(QStringLiteral("blocks")).toObject();
    QSet<QString> blockNames;
    QMap<QString, int> blockBytes; // 顺序块固定字节和；位域块 = 容器宽度字节
    for (auto it = blocks.begin(); it != blocks.end(); ++it)
    {
        const QString bname = it.key();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(bname).hasMatch())
            errs << QStringLiteral("块名不是标识符：%1").arg(bname);
        if (blockNames.contains(bname))
            errs << QStringLiteral("块名重复：%1").arg(bname);
        blockNames.insert(bname);

        const QJsonObject block = it.value().toObject();
        for (auto bk = block.begin(); bk != block.end(); ++bk)
            if (bk.key() != QLatin1String("of") && bk.key() != QLatin1String("description") &&
                bk.key() != QLatin1String("fields"))
                errs << QStringLiteral("块 %1 有未知键：%2").arg(bname, bk.key());

        const QJsonArray fields = block.value(QStringLiteral("fields")).toArray();
        if (fields.isEmpty())
            errs << QStringLiteral("块 %1 的 fields 不能为空").arg(bname);

        const QString of = block.value(QStringLiteral("of")).toString();
        int containerBytes = 0;
        if (!of.isEmpty())
        {
            if (!isIntegerType(of))
                errs << QStringLiteral("块 %1 的 of 必须是整型（u8..u64/i8..i64）").arg(bname);
            containerBytes = typeBytes(of);
        }

        int totalBits = 0;
        QList<QPair<int, int>> spans; // 位域块：已占用的 [off, off+width)
        int sumBytes = 0;
        for (const QJsonValue &fv : fields)
        {
            const QJsonObject f = fv.toObject();
            const QString fname = f.value(QStringLiteral("name")).toString().trimmed();
            const QString owner = QStringLiteral("块 %1 的字段 %2").arg(bname, fname.isEmpty() ? QStringLiteral("(无名)") : fname);
            if (fname.isEmpty())
                errs << QStringLiteral("块 %1 有字段缺少 name").arg(bname);
            else if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(fname).hasMatch())
                errs << QStringLiteral("%1 的名字不是标识符").arg(owner);
            checkUnknownKeys(f, binSegKeys(), owner, errs);

            const QString type = f.value(QStringLiteral("type")).toString().toLower();
            if (type.isEmpty())
                errs << QStringLiteral("%1 缺少 type").arg(owner);

            if (!of.isEmpty())
            {
                // 位域块：子字段只允许 bit + 宽@偏移，且不得重叠、不得超出容器
                if (type != QLatin1String("bit"))
                    errs << QStringLiteral("%1 在位域块里必须是 type:bit").arg(owner);
                const QJsonValue cv = f.value(QStringLiteral("count"));
                int w = 0;
                int off = 0;
                if (!cv.isString() || !parseBitSpan(cv.toString(), &w, &off))
                {
                    errs << QStringLiteral("%1 的 count 必须写成 \"宽@偏移\"").arg(owner);
                }
                else
                {
                    QString why;
                    if (!bitSpanOk(w, off, &why))
                        errs << QStringLiteral("%1：%2").arg(owner, why);
                    if ((off + w) > containerBytes * 8)
                        errs << QStringLiteral("%1 超出容器宽度 %2 位").arg(owner).arg(containerBytes * 8);
                    for (const auto &s : spans)
                        if (off < s.second && s.first < off + w)
                            errs << QStringLiteral("%1 的位区间与前面字段重叠").arg(owner);
                    spans << qMakePair(off, off + w);
                    totalBits += w;
                    (void)totalBits;
                }
            }
            else
            {
                // 顺序块：正常字节段，块内不允许再引用块（只一层）
                if (blockNames.contains(type))
                    errs << QStringLiteral("%1 不能再引用块 %2（块只允许一层）").arg(owner, type);
                const QJsonValue cv = f.value(QStringLiteral("count"));
                int n = 1;
                if (cv.isString() && cv.toString() == QLatin1String("*"))
                {
                    ctx.hasVariable = true;
                    sumBytes = -1;
                }
                else if (!cv.isUndefined() && !literalSize(cv, &n))
                {
                    ctx.hasVariable = true;
                    sumBytes = -1;
                }
                if (type == QLatin1String("bit"))
                    errs << QStringLiteral("%1 是顺序块字段，不能用 type:bit（位域请用 of）").arg(owner);
                if (sumBytes >= 0)
                    sumBytes += n * typeBytes(type);
            }
        }
        blockBytes.insert(bname, sumBytes < 0 ? 0 : (of.isEmpty() ? sumBytes : containerBytes));
        if (sumBytes < 0)
            ctx.hasVariable = true;
    }

    // segments
    const QJsonArray segArr = root.value(QStringLiteral("segments")).toArray();
    if (segArr.isEmpty())
        errs << QStringLiteral("segments 不能为空");

    int index = 0;
    int wildcardCount = 0;
    int wildcardIndex = -1;
    for (const QJsonValue &sv : segArr)
    {
        const QJsonObject seg = sv.toObject();
        const QString name = seg.value(QStringLiteral("name")).toString().trimmed();
        const int selfIndex = index;
        if (name.isEmpty())
        {
            errs << QStringLiteral("第 %1 段缺少 name").arg(index + 1);
            ++index;
            continue;
        }
        if (ctx.names.contains(name))
            errs << QStringLiteral("段名重复：%1").arg(name);
        ctx.names.insert(name);
        ctx.order.insert(name, index++);
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(name).hasMatch())
            errs << QStringLiteral("段名只能是标识符：%1").arg(name);

    checkUnknownKeys(seg, binSegKeys(), name, errs);
    for (const char *k : {"name", "description", "unit", "output", "type", "on_mismatch", "if"})
        expectString(seg, k, name, errs);

        const QString output = seg.value(QStringLiteral("output")).toString(QStringLiteral("channel"));
        if (output != QLatin1String("channel") && output != QLatin1String("aux") && output != QLatin1String("none"))
            errs << QStringLiteral("%1 的 output 只能是 channel/aux/none").arg(name);
        const QString onm = seg.value(QStringLiteral("on_mismatch")).toString(QStringLiteral("reject"));
        if (onm != QLatin1String("reject") && onm != QLatin1String("warn") && onm != QLatin1String("skip"))
            errs << QStringLiteral("%1 的 on_mismatch 只能是 reject/warn/skip").arg(name);
        if (seg.contains(QStringLiteral("if")))
        {
            const QString cond = seg.value(QStringLiteral("if")).toString();
            if (cond.trimmed().isEmpty())
                errs << QStringLiteral("%1 的 if 不能为空").arg(name);
            else
                checkExprRefs(cond, name + QStringLiteral(".if"), ctx, selfIndex, errs);
        }

        const QString type = seg.value(QStringLiteral("type")).toString().toLower();
        const QJsonValue cv = seg.value(QStringLiteral("count"));

        if (type.isEmpty())
            errs << QStringLiteral("%1 缺少 type").arg(name);
        const bool isBit = (type == QLatin1String("bit"));
        const bool isBlock = blockNames.contains(type);
        const bool isBuiltin = typeNames().contains(type) || aliasNames.contains(type);

        if (!isBit && !isBlock && !isBuiltin)
            errs << QStringLiteral("%1 的类型未知：%2").arg(name, type);

        int bytes = 0;
        if (isBit)
        {
            int w = 0;
            int off = 0;
            if (!cv.isString() || !parseBitSpan(cv.toString(), &w, &off))
                errs << QStringLiteral("%1 是 bit 类型，count 必须写成 \"宽@偏移\"（如 16@8）").arg(name);
            else
            {
                QString why;
                if (!bitSpanOk(w, off, &why))
                    errs << QStringLiteral("%1：%2").arg(name, why);
            }
            bytes = 0; // 位域不推进字节游标
        }
        else if (isBlock)
        {
            if (cv.isString() && cv.toString() == QLatin1String("*"))
            {
                ctx.hasVariable = true;
                ++wildcardCount;
                wildcardIndex = selfIndex;
            }
            else if (cv.isUndefined())
            {
                bytes = blockBytes.value(type, 0);
            }
            else
            {
                int n = 0;
                if (literalSize(cv, &n))
                    bytes = n * blockBytes.value(type, 0);
                else
                    ctx.hasVariable = true;
            }
        }
        else
        {
            if (cv.isString() && cv.toString() == QLatin1String("*"))
            {
                ctx.hasVariable = true;
                ++wildcardCount;
                wildcardIndex = selfIndex;
            }
            else if (cv.isUndefined())
                bytes = typeBytes(type);
            else
            {
                int n = 0;
                if (literalSize(cv, &n))
                    bytes = n * typeBytes(type);
                else if (cv.isString() && !cv.toString().trimmed().isEmpty())
                {
                    ctx.hasVariable = true;
                    checkExprRefs(cv.toString(), name + QStringLiteral(".count"), ctx, selfIndex, errs);
                }
                else
                    errs << QStringLiteral("%1 的 count 必须是正整数、变量、表达式或 *").arg(name);
            }
        }

        if (bytes > 0)
            ctx.fixedBytes.insert(name, bytes);
        ctx.segTypes.insert(name, type);

        if (seg.contains(QStringLiteral("transform")))
            checkTransform(seg.value(QStringLiteral("transform")), name, ctx, selfIndex, errs);
        if (seg.contains(QStringLiteral("expect")))
            checkExpect(seg.value(QStringLiteral("expect")), name, ctx, selfIndex,
                        ctx.fixedBytes.value(name, -1), errs);
    }

    // count:"*" 只能有一处：两处无法确定各自读多少
    if (wildcardCount > 1)
        errs << QStringLiteral("count:\"*\" 只能有一处（帧尾只能被一个段吞）");
    // "*" 之后只允许固定长度的尾部段，用于给帧尾 CRC 之类留位
    if (wildcardIndex >= 0 && wildcardIndex < segArr.size() - 1)
    {
        for (int j = wildcardIndex + 1; j < segArr.size(); ++j)
        {
            const QJsonObject after = segArr.at(j).toObject();
            const QString aname = after.value(QStringLiteral("name")).toString();
            if (!ctx.fixedBytes.contains(aname))
                errs << QStringLiteral("段 %1 用了 count:\"*\"，其后只能跟固定长度段（%2 不是）")
                            .arg(segArr.at(wildcardIndex).toObject().value(QStringLiteral("name")).toString(), aname);
        }
    }

    // derived：顶层派生量，按顺序求值
    const QJsonArray derived = root.value(QStringLiteral("derived")).toArray();
    QSet<QString> derivedNames;
    for (const QJsonValue &dv : derived)
    {
        const QJsonObject d = dv.toObject();
        const QString dname = d.value(QStringLiteral("name")).toString().trimmed();
        if (dname.isEmpty())
        {
            errs << QStringLiteral("derived 里有条目缺少 name");
            continue;
        }
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(dname).hasMatch())
            errs << QStringLiteral("derived 名字不是标识符：%1").arg(dname);
        if (ctx.names.contains(dname) || derivedNames.contains(dname))
            errs << QStringLiteral("derived 与段/派生重名：%1").arg(dname);
        for (auto it = d.begin(); it != d.end(); ++it)
            if (it.key() != QLatin1String("name") && it.key() != QLatin1String("expr") &&
                it.key() != QLatin1String("unit") && it.key() != QLatin1String("output") &&
                it.key() != QLatin1String("description"))
                errs << QStringLiteral("derived %1 有未知键：%2").arg(dname, it.key());
        const QString expr = d.value(QStringLiteral("expr")).toString();
        // 派生表达式可引用段与之前的派生量：把它们临时登记进 ctx
        for (const QString &dn : derivedNames)
            ctx.order.insert(dn, ctx.order.size());
        checkExprRefs(expr, QStringLiteral("derived %1").arg(dname), ctx, -1, errs);
        derivedNames.insert(dname);
        const QString out = d.value(QStringLiteral("output")).toString(QStringLiteral("channel"));
        if (out != QLatin1String("channel") && out != QLatin1String("aux") && out != QLatin1String("none"))
            errs << QStringLiteral("derived %1 的 output 只能是 channel/aux/none").arg(dname);
    }

    validateShared(root, ctx);
    return errs;
}

} // namespace nextpilot::plotscope::protocol
