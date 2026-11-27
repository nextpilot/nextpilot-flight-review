#pragma once

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::context {

// 解析器目录的宿主门面（M4 前置修：modules 不许直接 include ParserHub）。
//
// 只有 libs/context 能碰 libs/parser；模块/CLI 想知道"某个文件该用哪个格式、
// 注册表里有哪些后缀"，一律走这里，不自己写扩展名表。
//
// 线程与生命周期契约：解析器注册只发生在进程启动期（编译期自注册在 main 之前完成；
// ParserHub 的运行期 addPluginParser 当前无调用者），此后注册表按只读使用。
// 因此 suffixes()/patterns() 在首次查询时冻结快照，任意线程只读均安全；
// 若将来引入运行期热插拔，必须先在 ParserHub 层加读写锁并作废本快照。
namespace ParserCatalog {

// 后缀 → 解析器 id；未登记返回空串。前导点可省（"csv" 与 ".csv" 等价），大小写不敏感。
QString formatForSuffix(const QString &suffix);

// 路径的后缀 → 解析器 id；无后缀或未登记返回空串。
QString formatForPath(const QString &path);

// 已登记后缀，去重、带点小写（如 ".csv" ".ulg"）；启动期注册完成后首次查询冻结。
QStringList suffixes();

// 已登记后缀对应的收集 pattern（如 "*.csv" "*.ulg"）。
QStringList patterns();

} // namespace ParserCatalog

} // namespace nextpilot::plotscope::context
