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

// M1 顶层只认这两键；其余键（viewsContainers/customEditors/configuration/colors…）
// 属于 M2/M3，当前记一条 issue 跳过，不参与解析、不崩。
bool isKnownTopKey(const QString &key)
{
    return key == QLatin1String("commands") || key == QLatin1String("menus");
}

// command 条目允许的字段（多一个字段就记 issue 跳过该字段）。
bool isKnownCommandField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("command"), QStringLiteral("title"),
                                          QStringLiteral("category"), QStringLiteral("icon"),
                                          QStringLiteral("order")};
    return allowed.contains(key);
}

// menus.commandPalette 条目允许的字段：{command, group, when}。
bool isKnownPaletteField(const QString &key)
{
    static const QSet<QString> allowed = {QStringLiteral("command"), QStringLiteral("group"),
                                          QStringLiteral("when")};
    return allowed.contains(key);
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

    for (auto top = contributes.constBegin(); top != contributes.constEnd(); ++top)
    {
        const QString key = top.key();
        if (!isKnownTopKey(key))
        {
            addIssue(result, key, QStringLiteral("未知 contributes 键，跳过（M2/M3 键或拼错）"));
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
            QSet<QString> seen;
            for (int i = 0; i < arr.size(); ++i)
            {
                const QString path = QStringLiteral("commands[%1]").arg(i);
                if (!arr.at(i).isObject())
                {
                    addIssue(result, path, QStringLiteral("命令条目必须是对象，跳过"));
                    continue;
                }
                const QJsonObject obj = arr.at(i).toObject();

                // 未知字段：逐字段诊断并跳过该字段，其余字段照常解析。
                for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                    if (!isKnownCommandField(f.key()))
                        addIssue(result, path + QLatin1Char('.') + f.key(),
                                 QStringLiteral("命令条目未知字段，跳过"));

                const QJsonValue idv = obj.value(QStringLiteral("command"));
                if (!idv.isString() || idv.toString().isEmpty())
                {
                    addIssue(result, path + QStringLiteral(".command"), QStringLiteral("缺 command id，跳过该条"));
                    continue;
                }
                const QString id = idv.toString();
                if (seen.contains(id))
                {
                    addIssue(result, path + QStringLiteral(".command"),
                             QStringLiteral("重复 command id，后到者跳过（先到者保留）"));
                    continue;
                }
                seen.insert(id);

                ContributedCommand c;
                c.command = id;
                c.title = obj.value(QStringLiteral("title")).toString();
                c.category = obj.value(QStringLiteral("category")).toString();
                c.icon = obj.value(QStringLiteral("icon")).toString();
                const QJsonValue ov = obj.value(QStringLiteral("order"));
                if (ov.isDouble() || ov.isString())
                    c.order = ov.toVariant().toInt();
                result.doc.commands.append(c);
            }
        }
        // ---------- menus.commandPalette ----------
        else // key == "menus"
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
                if (placeKey != QLatin1String("commandPalette"))
                {
                    addIssue(result, QStringLiteral("menus.") + placeKey,
                             QStringLiteral("M1 未支持的菜单 place，跳过（M2 解锁）"));
                    continue;
                }
                if (!place.value().isArray())
                {
                    addIssue(result, QStringLiteral("menus.commandPalette"),
                             QStringLiteral("commandPalette 必须是数组，整条拒收"));
                    continue;
                }
                const QJsonArray arr = place.value().toArray();
                for (int i = 0; i < arr.size(); ++i)
                {
                    const QString path = QStringLiteral("menus.commandPalette[%1]").arg(i);
                    if (!arr.at(i).isObject())
                    {
                        addIssue(result, path, QStringLiteral("菜单条目必须是对象，跳过"));
                        continue;
                    }
                    const QJsonObject obj = arr.at(i).toObject();
                    for (auto f = obj.constBegin(); f != obj.constEnd(); ++f)
                        if (!isKnownPaletteField(f.key()))
                            addIssue(result, path + QLatin1Char('.') + f.key(),
                                     QStringLiteral("菜单条目未知字段，跳过"));

                    const QJsonValue idv = obj.value(QStringLiteral("command"));
                    if (!idv.isString() || idv.toString().isEmpty())
                    {
                        addIssue(result, path + QStringLiteral(".command"),
                                 QStringLiteral("缺 command，跳过该条"));
                        continue;
                    }
                    CommandPaletteMenuItem item;
                    item.command = idv.toString();
                    item.group = obj.value(QStringLiteral("group")).toString();
                    item.when = obj.value(QStringLiteral("when")).toString();
                    result.doc.commandPalette.append(item);
                }
            }
        }
    }

    return result;
}

} // namespace nextpilot::plotscope::registry
