#pragma once

#include "ps_parser_spi.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace nextpilot::plotscope::core
{

// 已登记的解析器。
struct ParserEntry
{
    PsParserInfo info{};              // 元数据副本（id/display_name/extensions 等）
    const PsParserVtable *vtable = nullptr;
    QString provider;                // 来源标识："builtin:<模块路径>" 或 "plugin:<文件名>"
    bool isPlugin = false;           // 是否来自外部插件（卸载时需释放句柄）
};

// 解析器注册表：进程内唯一。
//
// 两条发现路径汇入这里：
//   1) 内部扩展：解析器 TU 里静态自注册（NG_REGISTER_PARSER），编译进 app；
//   2) 外部插件：目录扫描 → dlopen → 取 PsParserEntryFn → ABI 校验 → 注册。
//
// 宿主与调用者对「解析器从哪来」完全无感。
class ParserRegistry
{
public:
    // === 注册 ===

    // 内部扩展静态自注册入口（宏调用）。
    static bool registerParser(const PsParserInfo &info, const PsParserVtable *vtable,
                               const QString &provider);

    // 外部插件注册：持有插件句柄（卸载时释放）。
    bool addPluginParser(const PsParserInfo &info, const PsParserVtable *vtable,
                         std::shared_ptr<void> pluginHandle, const QString &provider);

    // 撤销：按 id 移除（插件句柄随之释放）。
    bool unregisterParser(const QString &id);

    // === 查询 ===

    static const ParserEntry *entry(const QString &id);
    static const PsParserVtable *vtable(const QString &id);
    static QStringList ids();
    static int count();

    // 按文件后缀找（带点，小写，如 ".ulg"）。
    static const ParserEntry *findByExtension(const QString &suffix);

    // 按内容嗅探：给一段头字节，逐个问 can_sniff 的解析器。
    static const ParserEntry *findBySniff(const QByteArray &head);

    // 已登记但未能匹配任何已知规则的（诊断用）。
    static QStringList unresolvedExtensions();

    // 清空（仅测试用）。
    static void reset();

private:
    QHash<QString, ParserEntry> m_entries;
    QStringList m_order;                          // 登记顺序
    QHash<QString, std::shared_ptr<void>> m_handles; // 插件句柄（按 id）
};

// 内部扩展的静态自注册宏。
//
// 用法（解析器 TU 末尾一行）：
//   NG_REGISTER_PARSER(UlogParser)
//
// 其中 UlogParser 需提供：
//   static const PsParserInfo &parserInfo();
//   static const PsParserVtable *parserVtable();
#define NG_REGISTER_PARSER(Cls)                                                        \
    namespace {                                                                        \
    const bool g_ng_parser_reg_ =                                                      \
        ::nextpilot::plotscope::core::ParserRegistry::registerParser(                                    \
            Cls::parserInfo(), Cls::parserVtable(), QStringLiteral("builtin"));        \
    }

} // namespace nextpilot::plotscope::core
