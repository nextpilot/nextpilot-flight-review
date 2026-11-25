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

// 顶层已知键：M1 两键 + M2 五键；customEditors/configuration（M3）仍记 issue 跳过。
bool isKnownTopKey(const QString &key)
{
    static const QSet<QString> allowed = {
        QStringLiteral("commands"), QStringLiteral("menus"),
        QStringLiteral("viewsContainers"), QStringLiteral("views"),
        QStringLiteral("colors"), QStringLiteral("statusbar")};
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
    static const QSet<QString> allowed = {QStringLiteral("id"), QStringLiteral("name"),
                                          QStringLiteral("container"), QStringLiteral("when"),
                                          QStringLiteral("order")};
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
                if (!claimId(id, path + QStringLiteral(".id")))
                    continue;
                ContributedViewContainer c;
                c.id = id;
                c.title = obj.value(QStringLiteral("title")).toString();
                c.area = obj.value(QStringLiteral("area")).toString();
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
    }

    return result;
}

} // namespace nextpilot::plotscope::registry
