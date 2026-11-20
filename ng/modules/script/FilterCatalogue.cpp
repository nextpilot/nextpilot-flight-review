#include "FilterCatalogue.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <cmath>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

FilterCatalogue::FilterCatalogue(std::shared_ptr<FilterEngine> engine)
    : m_engine(std::move(engine))
{
    if (!m_engine)
        m_engine = makeLuaFilterEngine();
}

Result<int> FilterCatalogue::addBundledSource(const QByteArray &source, const QString &origin)
{
    auto classes = m_engine->inspectModule(source, origin);
    if (!classes)
        return fail(classes.error().code, classes.error().message);
    int added = 0;
    for (FilterDescriptor &cls : classes.value())
    {
        if (find(cls.id))
            continue; // duplicate id：首个注册胜出
        CatalogueEntry entry;
        entry.cls = std::move(cls);
        entry.origin = origin;
        entry.bundled = true;
        m_entries.push_back(std::move(entry));
        ++added;
    }
    return Result<int>(added);
}

const CatalogueEntry *FilterCatalogue::find(const QString &id) const
{
    for (const CatalogueEntry &e : m_entries)
        if (e.cls.id == id)
            return &e;
    return nullptr;
}

Result<std::unique_ptr<FilterInstance>> FilterCatalogue::makeProcessor(const QString &id,
                                                                       const QJsonObject &params) const
{
    const CatalogueEntry *entry = find(id);
    if (!entry)
        return fail(QStringLiteral("enofilter"), QStringLiteral("未知过滤器 id: %1").arg(id));
    return m_engine->createInstance(entry->cls, params);
}

Result<std::unique_ptr<FilterInstance>> FilterCatalogue::makeProcessorFromSource(const QByteArray &source,
                                                                                 const QString &id,
                                                                                 const QJsonObject &params) const
{
    auto classes = m_engine->inspectModule(source, QStringLiteral("embedded"));
    if (!classes)
        return fail(classes.error().code, classes.error().message);
    for (FilterDescriptor &cls : classes.value())
    {
        if (cls.id == id)
            return m_engine->createInstance(cls, params);
    }
    return fail(QStringLiteral("enofilter"), QStringLiteral("内嵌源无过滤器 id: %1").arg(id));
}

std::optional<QByteArray> FilterCatalogue::standaloneClassScript(const QString &id) const
{
    const CatalogueEntry *entry = find(id);
    // id 被拼进 Luau 源码，只准标识符字符，杜绝注入。
    auto identOk = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    if (!entry || id.isEmpty() || !std::all_of(id.begin(), id.end(), identOk))
        return std::nullopt;

    const QString quoted = QStringLiteral("\"%1\"").arg(id);
    // 1) 把完整多类源包进一个立即执行函数，得到 module（单类或类列表）。
    // 2) 找到 id 匹配的 class，包装它的 create：先用 descriptor 的 default 填 p，再套 given 覆盖。
    // 3) 返回该 class —— 布局恢复、插件式 transform 可直接 create(given)。
    const QByteArray script =
        "local module = (function()\n" + entry->cls.source.toUtf8() +
        "\nend)()\n"
        "local classes = module.id ~= nil and { module } or module\n"
        "for _, c in ipairs(classes) do\n"
        "  if c.id == " + quoted.toUtf8() + " then\n"
        "    local create = c.create\n"
        "    c.create = function(given)\n"
        "      local p = {}\n"
        "      for _, d in ipairs(c.parameters or {}) do p[d.name] = d.default end\n"
        "      for k, v in pairs(given or {}) do p[k] = v end\n"
        "      return create(p)\n"
        "    end\n"
        "    return c\n"
        "  end\n"
        "end\n"
        "error(\"no filter class \" .. " + quoted.toUtf8() + ")\n";
    return script;
}

QJsonObject FilterCatalogue::descriptorToJson(const FilterDescriptor &d)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), d.id);
    o.insert(QStringLiteral("name"), d.name);
    o.insert(QStringLiteral("description"), d.description);
    o.insert(QStringLiteral("version"), d.version);
    o.insert(QStringLiteral("outputKind"), d.outputKind);
    o.insert(QStringLiteral("origin"), d.origin);

    QJsonArray params;
    for (const FilterParamSpec &p : d.parameters)
    {
        QJsonObject po;
        po.insert(QStringLiteral("name"), p.name);
        QString type;
        switch (p.type)
        {
        case FilterParamType::Number:  type = QStringLiteral("number"); break;
        case FilterParamType::Integer: type = QStringLiteral("int"); break;
        case FilterParamType::Boolean: type = QStringLiteral("bool"); break;
        case FilterParamType::Enum:    type = QStringLiteral("enum"); break;
        case FilterParamType::String:  type = QStringLiteral("string"); break;
        case FilterParamType::Text:    type = QStringLiteral("text"); break;
        }
        po.insert(QStringLiteral("type"), type);
        po.insert(QStringLiteral("label"), p.label.isEmpty() ? p.name : p.label);
        po.insert(QStringLiteral("tooltip"), p.tooltip);
        po.insert(QStringLiteral("default"), p.defaultValue);
        if (p.hasMin) po.insert(QStringLiteral("min"), p.min);
        if (p.hasMax) po.insert(QStringLiteral("max"), p.max);
        if (p.hasStep) po.insert(QStringLiteral("step"), p.step);
        if (p.hasDecimals) po.insert(QStringLiteral("decimals"), p.decimals);
        if (!p.unit.isEmpty()) po.insert(QStringLiteral("unit"), p.unit);
        if (!p.visibleWhenParam.isEmpty())
        {
            po.insert(QStringLiteral("visibleWhenParam"), p.visibleWhenParam);
            po.insert(QStringLiteral("visibleWhenEquals"), p.visibleWhenEquals);
        }
        if (!p.values.isEmpty())
        {
            QJsonArray vals;
            for (const FilterEnumValue &ev : p.values)
                vals.append(QJsonObject{{QStringLiteral("value"), ev.value},
                                        {QStringLiteral("label"), ev.label}});
            po.insert(QStringLiteral("values"), vals);
        }
        params.append(po);
    }
    o.insert(QStringLiteral("parameters"), params);
    return o;
}

Result<QJsonObject> FilterCatalogue::applySeries(const QString &id, const QJsonObject &params,
                                                 const QVector<double> &t, const QVector<double> &v) const
{
    if (t.size() != v.size() || t.isEmpty())
        return fail(QStringLiteral("eargs"), QStringLiteral("过滤器输入序列为空或时间/值长度不一致"));

    auto made = makeProcessor(id, params);
    if (!made)
        return fail(made.error().code, made.error().message);
    std::unique_ptr<FilterInstance> inst = std::move(made.value());

    // 会话首样本重基：Lua 契约的 t 是"自首样本起秒数"，输出时间再加回 t0。
    const double t0 = t.first();
    QJsonArray outT, outV;
    for (int i = 0; i < t.size(); ++i)
    {
        FilterSampleResult r = inst->calculate(t.at(i) - t0, v.at(i));
        if (inst->failed())
            return fail(QStringLiteral("eruntime"), inst->error());
        if (r.suppress)
            continue;
        const double ot = r.hasExplicitTime ? t0 + r.outTime : t.at(i);
        if (!std::isfinite(ot) || !std::isfinite(r.value))
            return fail(QStringLiteral("enan"), QStringLiteral("过滤器 %1 产出非有限值").arg(id));
        outT.append(ot);
        outV.append(r.value);
    }
    QJsonObject out;
    out.insert(QStringLiteral("time"), outT);
    out.insert(QStringLiteral("value"), outV);
    return Result<QJsonObject>(out);
}

std::shared_ptr<FilterCatalogue> makeBundledFilterCatalogue()
{
    auto cat = std::make_shared<FilterCatalogue>(makeLuaFilterEngine());
    (void)cat->addBundledSource(bundledBuiltinFiltersSource(), QStringLiteral("bundled"));
    return cat;
}

} // namespace nextpilot::plotscope::script
