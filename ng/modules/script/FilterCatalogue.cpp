#include "FilterCatalogue.h"

#include <QJsonDocument>

#include <algorithm>

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

std::shared_ptr<FilterCatalogue> makeBundledFilterCatalogue()
{
    auto cat = std::make_shared<FilterCatalogue>(makeLuaFilterEngine());
    (void)cat->addBundledSource(bundledBuiltinFiltersSource(), QStringLiteral("bundled"));
    return cat;
}

} // namespace nextpilot::plotscope::script
