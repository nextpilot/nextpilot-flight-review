#include "ParserCatalog.h"

#include <QStringList>

#include <cstdio>

/*
 * ParserCatalog 验收（M4 前置修的门面，被 modules 与 CLI 共用）：
 *   - 后缀 → 解析器 id 来自注册表，未登记返回空串（不猜默认格式）
 *   - 后缀大小写不敏感；路径无后缀返回空串
 *   - suffixes()/patterns() 同源：pattern 恰为 "*" + 后缀，且无重复
 *   - 每次查询反映当前注册表（不缓存旧快照）
 */
static int g_fail = 0;
#define OK(cond, msg)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (cond)                                                                                                      \
            std::printf("[ok] %s\n", msg);                                                                             \
        else                                                                                                           \
        {                                                                                                              \
            std::printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                               \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

namespace ParserCatalog = nextpilot::plotscope::context::ParserCatalog;

int main()
{
    // 已登记后缀必须能在注册表里映射到非空 id；大小写不敏感。
    const QStringList suffixes = ParserCatalog::suffixes();
    OK(!suffixes.isEmpty(), "注册表至少登记一个后缀");
    for (const QString &ext : suffixes)
    {
        OK(ext.startsWith(QLatin1Char('.')), "后缀带前导点");
        OK(!ParserCatalog::formatForSuffix(ext).isEmpty(), "已登记后缀映射到解析器 id");
        OK(ParserCatalog::formatForSuffix(ext.toUpper()) == ParserCatalog::formatForSuffix(ext), "后缀大小写不敏感");
    }

    // 未登记后缀 / 无后缀路径一律空串，不回退默认格式。
    OK(ParserCatalog::formatForSuffix(QStringLiteral(".foo")).isEmpty(), "未登记后缀返回空串");
    OK(ParserCatalog::formatForPath(QStringLiteral("/tmp/junk.foo")).isEmpty(), "未登记扩展名的路径返回空串");
    OK(ParserCatalog::formatForPath(QStringLiteral("/tmp/noext")).isEmpty(), "无扩展名的路径返回空串");

    // 路径查询与后缀查询必须同源（带点、小写、取最后一段）。
    OK(ParserCatalog::formatForPath(QStringLiteral("/tmp/a.CSV")) ==
           ParserCatalog::formatForSuffix(QStringLiteral(".csv")),
       "路径查询按最后一段后缀、大小写不敏感");
    // 契约：前导点可省，与带点写法等价（评审改进项）。
    OK(ParserCatalog::formatForSuffix(QStringLiteral("csv")) == ParserCatalog::formatForSuffix(QStringLiteral(".csv")),
       "无前导点后缀与带点后缀等价");
    OK(ParserCatalog::formatForSuffix(QStringLiteral("CSV")) == ParserCatalog::formatForSuffix(QStringLiteral(".csv")),
       "无前导点且大写后缀等价");

    // patterns 恰为 "*" + suffixes，顺序一致且无重复。
    const QStringList patterns = ParserCatalog::patterns();
    OK(patterns.size() == suffixes.size(), "pattern 与后缀一一对应");
    for (int i = 0; i < patterns.size(); ++i)
        OK(patterns.at(i) == QStringLiteral("*") + suffixes.at(i), "pattern 由后缀派生");
    QStringList dedup = suffixes;
    dedup.removeDuplicates();
    OK(dedup.size() == suffixes.size(), "后缀无重复");

    std::printf(g_fail == 0 ? "全部通过\n" : "失败 %d 项\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
