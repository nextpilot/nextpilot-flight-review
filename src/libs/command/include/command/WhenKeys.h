#pragma once

// when 上下文键的**唯一表**（B 方案契约 §二 / D19）。
//
// 生产代码的 when 键一律用本表的点号命名（如 selection.kind），禁止在别处
// 硬编码裸键名。新键必须先登记到这里再使用——本文件即契约键表的可执行版本。
//
// 命名约定（点号命名空间，对齐契约而非 VS Code 内置键）：
//   <域>.<对象>        如 selection.kind / activity.active / dataset.count
//   item.*             只在 View 作用域解析（D19），由持有视图的模块写入
//
// 历史驼峰键（selectionKind/hasSelection/activeActivity 等）已在 c8 收尾统一到
// 本表；suggest() 保留映射供诊断提示，生产不再写入。

#include <QHash>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::command
{

class WhenKeys
{
public:
    // 契约键表（唯一来源）。
    static const QStringList &all()
    {
        static const QStringList keys = {
            QStringLiteral("region.active"),
            QStringLiteral("view.active"),
            QStringLiteral("selection.kind"),
            QStringLiteral("selection.count"),
            QStringLiteral("document.kind"),
            QStringLiteral("activity.active"),
            QStringLiteral("dataset.count"),
            QStringLiteral("source.count"),
            QStringLiteral("playback.state"),
            QStringLiteral("item.kind"),
            QStringLiteral("item.id"),
            QStringLiteral("item.multiSelect"),
            // 以下为壳运行时需要的状态键（同命名空间约定，登记在表内）。
            QStringLiteral("editor.active"),
            QStringLiteral("group.active"),
            QStringLiteral("script.running"),
            QStringLiteral("focus.area"),
        };
        return keys;
    }

    static bool isKnown(const QString &key) { return all().contains(key); }

    // 历史驼峰键 → 契约点号键的建议映射（仅诊断用）。
    static QString suggest(const QString &key)
    {
        static const QHash<QString, QString> kLegacy = {
            {QStringLiteral("selectionKind"), QStringLiteral("selection.kind")},
            {QStringLiteral("hasSelection"), QStringLiteral("selection.count")},
            {QStringLiteral("hasData"), QStringLiteral("dataset.count")},
            {QStringLiteral("activeActivity"), QStringLiteral("activity.active")},
            {QStringLiteral("activeEditor"), QStringLiteral("editor.active")},
            {QStringLiteral("activeGroup"), QStringLiteral("group.active")},
            {QStringLiteral("isScriptRunning"), QStringLiteral("script.running")},
            {QStringLiteral("focusArea"), QStringLiteral("focus.area")},
        };
        return kLegacy.value(key);
    }

    // D19：item.* 只在 View 作用域内解析（由持有视图的模块写入，切视图整层替换）。
    static bool isViewScoped(const QString &key) { return key.startsWith(QStringLiteral("item.")); }
};

} // namespace nextpilot::plotscope::command
