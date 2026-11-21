#pragma once

// 工具箱注册表：机制只有一种（Registry<T>），内容全是数据，挂进去就完事。
//
// 每种贡献一张 Registry<T> 表（uiActions / panels / scripts / dataAssets / docs /
// examples / packs），条目是纯数据 struct（见 Contribution.h）。挂载单位是 Pack——
// 一个纯数据清单（各类型列表 + 解析器），mount() 一次挂进去、unmount() 按
// provider 原子撤销，扩展之间互不牵连。
//
// 解析器是唯一的行为式条目（真有 parse 行为、所有权 unique_ptr），走单独通道，
// 视图与数据表并列。
//
// 两条发现路径都汇入这里：
//   - 内置包：各自 TU 静态自注册工厂（NG_REGISTER_PACK），宿主启动时 installBuiltinPacks()；
//   - 外部插件：插件扫描器加载后 mount() 进来。

#include "Contribution.h"
#include "IMessageParser.h"
#include "Registry.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace nextpilot::plotscope::registry
{

using nextpilot::plotscope::parser::IMessageParser;
using nextpilot::plotscope::parser::ParseOutput;

// 一个包的完整清单：纯数据（除解析器需所有权外，无任何逻辑）。
// 包作者填这份清单，mount() 把各列表分插进对应类型的表并自动写 provider/domain。
struct Pack
{
    QString id;      // 全局唯一，如 "pack.autopilot"
    QString name;    // 显示名，如 "飞控工具箱"
    Domain domain = Domain::General;

    QList<UiAction> uiActions;
    QList<PanelSpec> panels;
    QList<ScriptSpec> scripts;
    QList<DataAsset> dataAssets;
    QList<DocEntry> docs;
    QList<ExampleEntry> examples;
    QList<ReportTemplate> reports;

    // 行为式条目：解析器（唯一有行为的贡献，所有权归包/注册表）。
    std::vector<std::unique_ptr<IMessageParser>> parsers;
};

class ExtensionRegistry
{
public:
    // === 内置包静态自注册通道 ===
    using PackFactory = std::function<std::unique_ptr<Pack>()>;
    static bool registerBuiltinPack(const QString &id, PackFactory factory);
    QStringList installBuiltinPacks();        // 装进本实例，返回成功的包 id
    static void resetBuiltinPacks();          // 清空静态表（仅测试用）

    // === 挂载/卸载：唯一的动作对，按包原子 ===
    // 校验：包 id 与全部条目 id 全局唯一；挂载时给每个条目自动写 provider/domain。
    bool mount(Pack pack);
    bool unmount(const QString &packId);

    // === 数据表（机制唯一：Registry<T>；内容全是数据，直接读）===
    Registry<PackInfo> packs;        // 包自我声明
    Registry<UiAction> uiActions;
    Registry<PanelSpec> panels;
    Registry<ScriptSpec> scripts;
    Registry<DataAsset> dataAssets;
    Registry<DocEntry> docs;
    Registry<ExampleEntry> examples;
    Registry<ReportTemplate> reports;

    // === 解析器视图（行为式条目，所有权在注册表）===
    QList<IMessageParser *> parsers() const;
    QList<IMessageParser *> parsersByDomain(Domain domain) const;
    IMessageParser *parser(const QString &id) const;
    IMessageParser *parserForProtocol(const QString &protocolId) const;
    QList<IMessageParser *> parsersByTransport(const QString &transport) const;

    // === 旧 API 兼容别名（内部筛 outputs() 含 FrameBatch 的解析器）===
    bool registerDriver(std::unique_ptr<IMessageParser> driver);
    bool unregisterDriver(const QString &id);
    IMessageParser *driverForFormat(const QString &format) const;
    IMessageParser *driverForPath(const QString &path) const;
    QList<IMessageParser *> drivers() const;
    QList<IMessageParser *> driversByDomain(Domain domain) const;
    QList<Domain> domains() const;
    QStringList ids() const;
    bool contains(const QString &id) const;
    int count() const;

    // === 协议视图别名（供 ProtocolInspectorDialog / CLI）===
    QList<IMessageParser *> messageParsers() const { return parsers(); }
    QList<IMessageParser *> messageParsersByDomain(Domain domain) const { return parsersByDomain(domain); }
    IMessageParser *messageParser(const QString &id) const { return parser(id); }
    IMessageParser *messageParserForProtocol(const QString &protocolId) const { return parserForProtocol(protocolId); }
    QList<IMessageParser *> messageParsersByTransport(const QString &transport) const { return parsersByTransport(transport); }

private:
    std::vector<std::unique_ptr<IMessageParser>> m_parsers; // 全部解析器（含旧 driver 包装包的）
};

} // namespace nextpilot::plotscope::registry

// 注册宏：在包 TU 文件末尾使用，把本包工厂登记进静态自注册表。
//
// 用法：
//   NG_REGISTER_PACK(nextpilot::plotscope::ext::AutopilotPack)
//
// 其中 AutopilotPack 需提供：
//   static QString packId();
//   static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
//
// 宏必须带完整命名空间：它在调用处的命名空间作用域展开。
#define NG_REGISTER_PACK(FQN)                                                             \
    namespace {                                                                           \
    const bool g_ng_pack_reg_ =                                                           \
        ::nextpilot::plotscope::registry::ExtensionRegistry::registerBuiltinPack(         \
            FQN::packId(),                                                                \
            []() -> std::unique_ptr<::nextpilot::plotscope::registry::Pack> { return FQN::build(); }); \
    }
