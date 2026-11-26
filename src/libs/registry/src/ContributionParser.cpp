#include "ContributionParser.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace nextpilot::plotscope::registry
{
namespace
{
void addIssue(ParseResult &r, const QString &keyPath, const QString &msg)
{
    r.ok = false;
    r.issues.append(ParseIssue{keyPath, msg});
}

// 顶层已知键：M1 两键 + M2 五键 + M3-a 的 customEditors + M3-b 的 configuration。
bool isKnownTopKey(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("commands"),        QStringLiteral("menus"),
                                          QStringLiteral("viewsContainers"), QStringLiteral("views"),
                                          QStringLiteral("colors"),          QStringLiteral("statusbar"),
                                          QStringLiteral("customEditors"),   QStringLiteral("configuration")};
    return allowed.contains(key);
}

bool isKnownCommandField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("command"), QStringLiteral("title"),
                                          QStringLiteral("category"), QStringLiteral("icon"),
                                          QStringLiteral("order")};
    return allowed.contains(key);
}

// menus 条目通用字段。
bool isKnownMenuField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("command"), QStringLiteral("group"),
                                          QStringLiteral("when"), QStringLiteral("label"),
                                          QStringLiteral("icon"), QStringLiteral("order")};
    return allowed.contains(key);
}

bool isKnownViewContainerField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("id"), QStringLiteral("title"),
                                          QStringLiteral("area"), QStringLiteral("icon"),
                                          QStringLiteral("order")};
    return allowed.contains(key);
}

bool isKnownViewField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("id"),   QStringLiteral("name"), QStringLiteral("container"),
                                          QStringLiteral("icon"), QStringLiteral("when"), QStringLiteral("order")};
    return allowed.contains(key);
}

bool isKnownStatusField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("id"), QStringLiteral("side"),
                                          QStringLiteral("priority"), QStringLiteral("when"),
                                          QStringLiteral("icon"), QStringLiteral("tooltip"),
                                          QStringLiteral("commandId"), QStringLiteral("text")};
    return allowed.contains(key);
}

bool isKnownCustomEditorField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("viewType"),
                                          QStringLiteral("displayName"),
                                          QStringLiteral("selector"), QStringLiteral("priority")};
    return allowed.contains(key);
}

bool isKnownSelectorField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("mime"), QStringLiteral("filenamePattern")};
    return allowed.contains(key);
}

// configuration 条目字段（§4.1 白名单 + title/iconId/order/properties 容器字段）。
bool isKnownConfigurationField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("iconId"),
                                          QStringLiteral("order"), QStringLiteral("properties")};
    return allowed.contains(key);
}

// configuration 属性字段白名单（§4.1：type/default/enum/enumDescriptions/minimum/
// maximum/pattern/description/markdownDescription/scope/order/editPresentation + category）。
bool isKnownConfigurationPropertyField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("type"),
                                          QStringLiteral("default"),
                                          QStringLiteral("enum"),
                                          QStringLiteral("enumDescriptions"),
                                          QStringLiteral("minimum"),
                                          QStringLiteral("maximum"),
                                          QStringLiteral("pattern"),
                                          QStringLiteral("description"),
                                          QStringLiteral("markdownDescription"),
                                          QStringLiteral("scope"),
                                          QStringLiteral("order"),
                                          QStringLiteral("editPresentation"),
                                          QStringLiteral("category")};
    return allowed.contains(key);
}

// §4.1：tab/section/horizontal 这类布局节点禁入 configuration——排版归工作台（§5）。
bool isForbiddenLayoutNode(const QString &key)
{
    static const QSet<QString> forbidden = {QStringLiteral("tab"), QStringLiteral("section"),
                                            QStringLiteral("horizontal")};
    return forbidden.contains(key);
}

// §3.0.12：viewsContainers/views 只能落 sidebar/panel(bottom)/auxiliary/ribbon——
// 中央区只接受编辑器。area 恒无 "central"，出现即拒该条 + 诊断（防回归）。
bool isKnownContainerArea(const QString &area)
{
    static const QSet<QString> allowed = {QStringLiteral("sidebar"), QStringLiteral("bottom"),
                                          QStringLiteral("auxiliary"), QStringLiteral("ribbon")};
    return area.isEmpty() || allowed.contains(area);
}

// 校验 id 字段：空/非字符串 → 记 issue 返回 false。
bool requireId(const QJsonObject &obj, const QString &path, const QString &field,
               ParseResult &r)
{
    const QJsonValue v = obj.value(field);
    if (!v.isString() || v.toString().isEmpty())
    {
        addIssue(r, path + QLatin1Char('.') + field,
                 QStringLiteral("缺 %1（或类型不符），跳过该条").arg(field));
        return false;
    }
    return true;
}

// 读一个整数字段（number/string 兼容），非法保持默认 0。
int readInt(const QJsonObject &obj, const QString &key)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble() || v.isString())
        return v.toVariant().toInt();
    return 0;
}
} // namespace

ParseResult parseContributes(const QJsonObject &contributes, const QString &owner)
{
    ParseResult result;
    result.doc.owner = owner;

    // 畸形 JSON：contributes 本身不是对象 → 整条拒收。
    if (contributes.isEmpty())
    {
        addIssue(result, QStringLiteral("contributes"), QStringLiteral("contributes 为空或非对象，整条拒收"));
        return result;
    }

    // 全局 id 唯一（§3）：命令 / 视图 / 容器 / 颜色 / 状态栏 id 共用一张表。
    QSet<QString> seenIds;
    auto claimId = [&seenIds, &result](const QString &id, const QString &path) -> bool {
        if (id.isEmpty())
            return false;
        if (seenIds.contains(id))
        {
            addIssue(result, path, QStringLiteral("重复 id，后到者跳过（先到者保留）"));
            return false;
        }
        seenIds.insert(id);
        return true;
    };

    for (auto top = contributes.constBegin(); top != contributes.constEnd(); ++top)
    {
        const QString key = top.key();
        if (!isKnownTopKey(key))
        {
            addIssue(result, key, QStringLiteral("未知 contributes 键，跳过（M3 键或拼错）"));
            continue;
        }

        // ---------- commands ----------
        if (key == QLatin1String("commands"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, QStringLiteral("commands"), QStringLiteral("commands 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("commands[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("命令条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownCommandField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("命令条目未知字段，跳过"));

                if (!requireId(obj, path, QStringLiteral("command"), result))
                    continue;
                const QString id = obj.value(QStringLiteral("command")).toString();
                if (!claimId(id, path + QStringLiteral(".command")))
                    continue;

                ContributedCommand c;
                c.command = id;
                c.title = obj.value(QStringLiteral("title")).toString();
                c.category = obj.value(QStringLiteral("category")).toString();
                c.icon = obj.value(QStringLiteral("icon")).toString();
                c.order = readInt(obj, QStringLiteral("order"));
                result.doc.commands.append(c);
            }
        }
        // ---------- menus ----------
        else if (key == QLatin1String("menus"))
        {
            if (!top.value().isObject())
            {
                addIssue(result, QStringLiteral("menus"), QStringLiteral("menus 必须是对象，整条拒收"));
                continue;
            }
            const QJsonObject menus = top.value().toObject();
            for (auto place = menus.constBegin(); place != menus.constEnd(); ++place)
            {
                const QString placeKey = place.key();
                // 固定 place 枚举 + menubar/<pageId> 前缀（§3.0.1 勘误④）。
                const bool knownPlace =
                    placeKey == QLatin1String("commandPalette") ||
                    placeKey == QLatin1String("editor/context") ||
                    placeKey == QLatin1String("view/title") ||
                    placeKey == QLatin1String("view/item/context") ||
                    placeKey == QLatin1String("statusbar") ||
                    placeKey.startsWith(QLatin1String("menubar/"));
                if (!knownPlace)
                {
                    addIssue(result, QStringLiteral("menus.") + placeKey,
                             QStringLiteral("未支持的菜单 place，跳过"));
                    continue;
                }
                if (!place.value().isArray())
                {
                    addIssue(result, QStringLiteral("menus.") + placeKey,
                             QStringLiteral("菜单 place 必须是数组，整条拒收"));
                    continue;
                }
                const QJsonArray arr = place.value().toArray();
                for (int i = 0; i < arr.size(); ++i)
                {
                    const QString path = QStringLiteral("menus.%1[%2]").arg(placeKey).arg(i);
                    if (!arr.at(i).isObject())
                    {
                        addIssue(result, path, QStringLiteral("菜单条目必须是对象，跳过"));
                        continue;
                    }
                    const QJsonObject obj = arr.at(i).toObject();
                    for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                        if (!isKnownMenuField(f.key()))
                            addIssue(result, path + QLatin1Char('.') + f.key(),
                                     QStringLiteral("菜单条目未知字段，跳过"));

                    if (!requireId(obj, path, QStringLiteral("command"), result))
                        continue;
                    // commandPalette 保持 M1 专用结构（向后兼容 ExtensionActivationService）。
                    if (placeKey == QLatin1String("commandPalette"))
                    {
                        CommandPaletteMenuItem item;
                        item.command = obj.value(QStringLiteral("command")).toString();
                        item.group = obj.value(QStringLiteral("group")).toString();
                        item.when = obj.value(QStringLiteral("when")).toString();
                        result.doc.commandPalette.append(item);
                    }
                    else
                    {
                        ContributedMenuItem item;
                        item.place = placeKey;
                        item.command = obj.value(QStringLiteral("command")).toString();
                        item.group = obj.value(QStringLiteral("group")).toString();
                        item.when = obj.value(QStringLiteral("when")).toString();
                        item.label = obj.value(QStringLiteral("label")).toString();
                        item.icon = obj.value(QStringLiteral("icon")).toString();
                        item.order = readInt(obj, QStringLiteral("order"));
                        result.doc.menuItems.append(item);
                    }
                }
            }
        }
        // ---------- viewsContainers ----------
        else if (key == QLatin1String("viewsContainers"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, key, QStringLiteral("viewsContainers 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("viewsContainers[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("容器条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownViewContainerField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("容器条目未知字段，跳过"));
                if (!requireId(obj, path, QStringLiteral("id"), result))
                    continue;
                const QString id = obj.value(QStringLiteral("id")).toString();
                // area 白名单（§3.0.12）：中央区只接受编辑器；非法 area 拒该条且不占坑。
                const QString area = obj.value(QStringLiteral("area")).toString();
                if (!isKnownContainerArea(area))
                {
                    addIssue(result, path + QStringLiteral(".area"),
                             QStringLiteral("area 只认 sidebar/bottom/auxiliary/ribbon，"
                                            "中央区只接受编辑器，拒该条"));
                    continue;
                }
                if (!claimId(id, path + QStringLiteral(".id")))
                    continue;
                ContributedViewContainer c;
                c.id = id;
                c.title = obj.value(QStringLiteral("title")).toString();
                c.area = area;
                c.icon = obj.value(QStringLiteral("icon")).toString();
                c.order = readInt(obj, QStringLiteral("order"));
                result.doc.viewContainers.append(c);
            }
        }
        // ---------- views ----------
        else if (key == QLatin1String("views"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, key, QStringLiteral("views 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("views[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("视图条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownViewField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("视图条目未知字段，跳过"));
                if (!requireId(obj, path, QStringLiteral("id"), result))
                    continue;
                const QString id = obj.value(QStringLiteral("id")).toString();
                if (!claimId(id, path + QStringLiteral(".id")))
                    continue;
                ContributedView v;
                v.id = id;
                v.name = obj.value(QStringLiteral("name")).toString();
                v.container = obj.value(QStringLiteral("container")).toString();
                v.icon = obj.value(QStringLiteral("icon")).toString();
                v.when = obj.value(QStringLiteral("when")).toString();
                v.order = readInt(obj, QStringLiteral("order"));
                result.doc.views.append(v);
            }
        }
        // ---------- colors ----------
        else if (key == QLatin1String("colors"))
        {
            if (!top.value().isObject())
            {
                addIssue(result, key, QStringLiteral("colors 必须是对象，整条拒收"));
                continue;
            }
            const QJsonObject colors = top.value().toObject();
            for (auto token = colors.constBegin(); token != colors.constEnd(); ++token)
            {
                const QString path = QStringLiteral("colors.%1").arg(token.key());
                if (!token.value().isObject())
                {
                    addIssue(result, path, QStringLiteral("颜色 token 必须是对象，跳过"));
                    continue;
                }
                if (!claimId(token.key(), path))
                    continue;
                const QJsonObject obj = token.value().toObject();
                ContributedColor c;
                c.id = token.key();
                c.dark = obj.value(QStringLiteral("dark")).toString();
                c.light = obj.value(QStringLiteral("light")).toString();
                result.doc.colors.append(c);
            }
        }
        // ---------- statusbar ----------
        else if (key == QLatin1String("statusbar"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, key, QStringLiteral("statusbar 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("statusbar[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("状态栏条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownStatusField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("状态栏条目未知字段，跳过"));
                if (!requireId(obj, path, QStringLiteral("id"), result))
                    continue;
                const QString id = obj.value(QStringLiteral("id")).toString();
                if (!claimId(id, path + QStringLiteral(".id")))
                    continue;
                ContributedStatus s;
                s.id = id;
                s.side = obj.value(QStringLiteral("side")).toString();
                s.priority = readInt(obj, QStringLiteral("priority"));
                s.when = obj.value(QStringLiteral("when")).toString();
                s.icon = obj.value(QStringLiteral("icon")).toString();
                s.tooltip = obj.value(QStringLiteral("tooltip")).toString();
                s.commandId = obj.value(QStringLiteral("commandId")).toString();
                s.text = obj.value(QStringLiteral("text")).toString();
                result.doc.statuses.append(s);
            }
        }
        // ---------- customEditors ----------
        else if (key == QLatin1String("customEditors"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, key, QStringLiteral("customEditors 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("customEditors[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("编辑器条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownCustomEditorField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("编辑器条目未知字段，跳过"));
                if (!requireId(obj, path, QStringLiteral("viewType"), result))
                    continue;
                const QString id = obj.value(QStringLiteral("viewType")).toString();
                // selector 是 VSCode required 字段：校验全部前移到 claimId 之前——
                // 整条被拒时不占 seenIds 坑，后到的同 viewType 合法条目可递补
                // （裁定 52443049；seenIds 命令/视图/颜色/状态栏/编辑器共用，无回退手段）。
                if (!obj.value(QStringLiteral("selector")).isArray())
                {
                    addIssue(result, path + QStringLiteral(".selector"),
                             QStringLiteral("selector 缺失或非法，整条拒"));
                    continue;
                }
                ContributedCustomEditor e;
                e.viewType = id;
                e.displayName = obj.value(QStringLiteral("displayName")).toString();
                e.priority = readInt(obj, QStringLiteral("priority"));
                const QJsonArray sels = obj.value(QStringLiteral("selector")).toArray();
                for (int j = 0; j < sels.size(); ++j)
                {
                    const QString selPath = path + QStringLiteral(".selector[%1]").arg(j);
                    if (!sels.at(j).isObject())
                    {
                        addIssue(result, selPath, QStringLiteral("selector 条目必须是对象，跳过"));
                        continue;
                    }
                    const QJsonObject sobj = sels.at(j).toObject();
                    for (auto f = sobj.constBegin(); f != sobj.constEnd(); ++f)
                        if (!isKnownSelectorField(f.key()))
                            addIssue(result, selPath + QLatin1Char('.') + f.key(),
                                     QStringLiteral("selector 条目未知字段，跳过"));
                    ContributedEditorSelector sel;
                    sel.mime = sobj.value(QStringLiteral("mime")).toString();
                    sel.filenamePattern = sobj.value(QStringLiteral("filenamePattern")).toString();
                    if (sel.mime.isEmpty() && sel.filenamePattern.isEmpty())
                    {
                        addIssue(result, selPath,
                                 QStringLiteral("selector 缺 mime 与 filenamePattern，跳过"));
                        continue;
                    }
                    e.selector.append(sel);
                }
                if (e.selector.isEmpty())
                {
                    addIssue(result, path + QStringLiteral(".selector"),
                             QStringLiteral("selector 全非法，整条拒"));
                    continue;
                }
                if (!claimId(id, path + QStringLiteral(".viewType")))
                    continue;
                result.doc.customEditors.append(e);
            }
        }
        // ---------- configuration ----------
        else if (key == QLatin1String("configuration"))
        {
            if (!top.value().isArray())
            {
                addIssue(result, key, QStringLiteral("configuration 必须是数组，整条拒收"));
                continue;
            }
            const QJsonArray arr = top.value().toArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("configuration[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("configuration 条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();
                bool layoutRejected = false;
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                {
                    if (isForbiddenLayoutNode(f.key()))
                    {
                        // §4.1：布局节点禁入——诊断并拒该节点（整条），不是只跳字段。
                        addIssue(
                            result, path + QLatin1Char('.') + f.key(),
                            QStringLiteral("布局节点 %1 禁入 configuration，拒该节点（排版归工作台）").arg(f.key()));
                        layoutRejected = true;
                    }
                    else if (!isKnownConfigurationField(f.key()))
                    {
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("configuration 条目未知字段，跳过"));
                    }
                }
                if (layoutRejected)
                    continue;
                if (!obj.value(QStringLiteral("properties")).isObject())
                {
                    addIssue(result, path + QStringLiteral(".properties"),
                             QStringLiteral("properties 缺失或非法，拒该节点"));
                    continue;
                }
                ConfigurationSpec spec;
                spec.id = obj.value(QStringLiteral("id")).toString();
                spec.title = obj.value(QStringLiteral("title")).toString();
                spec.iconId = obj.value(QStringLiteral("iconId")).toString();
                spec.order = readInt(obj, QStringLiteral("order"));
                const QJsonObject props = obj.value(QStringLiteral("properties")).toObject();
                for (auto p = props.constBegin(); p != props.constEnd(); ++p)
                {
                    const QString propPath = path + QStringLiteral(".properties.") + p.key();
                    if (!p.value().isObject())
                    {
                        addIssue(result, propPath, QStringLiteral("属性条目必须是对象，跳过"));
                        continue;
                    }
                    const QJsonObject pobj = p.value().toObject();
                    bool propRejected = false;
                    for (auto f = pobj.constBegin(); f != pobj.constEnd(); ++f)
                    {
                        if (isForbiddenLayoutNode(f.key()))
                        {
                            addIssue(result, propPath + QLatin1Char('.') + f.key(),
                                     QStringLiteral("布局节点 %1 禁入 configuration 属性，拒该条").arg(f.key()));
                            propRejected = true;
                        }
                        else if (!isKnownConfigurationPropertyField(f.key()))
                        {
                            addIssue(result, propPath + QLatin1Char('.') + f.key(),
                                     QStringLiteral("配置属性未知字段，跳过"));
                        }
                    }
                    if (propRejected)
                        continue;
                    const QJsonValue typeV = pobj.value(QStringLiteral("type"));
                    if (!typeV.isString() || typeV.toString().isEmpty())
                    {
                        addIssue(result, propPath + QStringLiteral(".type"),
                                 QStringLiteral("缺 type（或类型不符），跳过该条"));
                        continue;
                    }
                    ConfigurationProperty prop;
                    prop.key = p.key();
                    prop.type = typeV.toString();
                    prop.defaultValue = pobj.value(QStringLiteral("default"));
                    prop.pattern = pobj.value(QStringLiteral("pattern")).toString();
                    prop.description = pobj.value(QStringLiteral("description")).toString();
                    prop.markdownDescription = pobj.value(QStringLiteral("markdownDescription")).toString();
                    prop.order = readInt(pobj, QStringLiteral("order"));
                    prop.editPresentation = pobj.value(QStringLiteral("editPresentation")).toString();
                    prop.category = pobj.value(QStringLiteral("category")).toString();
                    // scope：VSCode ConfigurationScope 六值域（缺省 window），非法值拒该条。
                    const QString scope = pobj.value(QStringLiteral("scope")).toString();
                    static const QSet<QString> allowedScopes = {QStringLiteral("application"),
                                                                QStringLiteral("machine"),
                                                                QStringLiteral("window"),
                                                                QStringLiteral("resource"),
                                                                QStringLiteral("language-overridable"),
                                                                QStringLiteral("machine-overridable")};
                    if (scope.isEmpty())
                        prop.scope = QStringLiteral("window");
                    else if (!allowedScopes.contains(scope))
                    {
                        addIssue(result, propPath + QStringLiteral(".scope"),
                                 QStringLiteral("scope 不在 application/machine/window/resource/"
                                                "language-overridable/machine-overridable 值域内，"
                                                "拒该条"));
                        continue;
                    }
                    else
                        prop.scope = scope;
                    // enum / enumDescriptions：给了就必须是数组（类型不符 → 拒该条）。
                    if (pobj.contains(QStringLiteral("enum")))
                    {
                        if (!pobj.value(QStringLiteral("enum")).isArray())
                        {
                            addIssue(result, propPath + QStringLiteral(".enum"),
                                     QStringLiteral("enum 必须是数组，拒该条"));
                            continue;
                        }
                        for (const auto &v : pobj.value(QStringLiteral("enum")).toArray())
                            prop.enumValues.append(v.toVariant().toString());
                    }
                    if (pobj.contains(QStringLiteral("enumDescriptions")))
                    {
                        if (!pobj.value(QStringLiteral("enumDescriptions")).isArray())
                        {
                            addIssue(result, propPath + QStringLiteral(".enumDescriptions"),
                                     QStringLiteral("enumDescriptions 必须是数组，拒该条"));
                            continue;
                        }
                        for (const auto &v : pobj.value(QStringLiteral("enumDescriptions")).toArray())
                            prop.enumDescriptions.append(v.toString());
                    }
                    // minimum / maximum：给了就必须是数字。
                    if (pobj.contains(QStringLiteral("minimum")))
                    {
                        if (!pobj.value(QStringLiteral("minimum")).isDouble())
                        {
                            addIssue(result, propPath + QStringLiteral(".minimum"),
                                     QStringLiteral("minimum 必须是数字，拒该条"));
                            continue;
                        }
                        prop.minimum = pobj.value(QStringLiteral("minimum")).toDouble();
                        prop.hasMinimum = true;
                    }
                    if (pobj.contains(QStringLiteral("maximum")))
                    {
                        if (!pobj.value(QStringLiteral("maximum")).isDouble())
                        {
                            addIssue(result, propPath + QStringLiteral(".maximum"),
                                     QStringLiteral("maximum 必须是数字，拒该条"));
                            continue;
                        }
                        prop.maximum = pobj.value(QStringLiteral("maximum")).toDouble();
                        prop.hasMaximum = true;
                    }
                    // 整条校验通过后再 claimId：被拒条目不占坑，
                    // 后到的同 key 合法条目可递补；seenIds 与命令/视图/编辑器共用。
                    if (!claimId(prop.key, propPath))
                        continue;
                    spec.properties.append(prop);
                }
                if (spec.properties.isEmpty())
                {
                    addIssue(result, path + QStringLiteral(".properties"), QStringLiteral("无合法属性，拒该节点"));
                    continue;
                }
                result.doc.configurations.append(spec);
            }
        }
    }

    return result;
}

} // namespace nextpilot::plotscope::registry
