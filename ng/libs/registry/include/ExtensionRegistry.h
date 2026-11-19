#pragma once

#include "IndexedByKind.h"
#include "IToolContribution.h"
#include "IToolPack.h"
#include "IMessageParser.h"
#include "Domain.h"
#include "Registry.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace nextpilot::plotscope::registry
{

using nextpilot::plotscope::parser::IMessageParser;
using nextpilot::plotscope::parser::ParseOutput;

// 工具箱注册表：所有知识包 / 贡献的唯一登记处。
//
// 从"格式驱动注册表"泛化为"开放贡献点系统"：
//   - 一次登记的单位是 IToolPack（知识包），它聚合任意 kind 的贡献；
//   - 贡献按 (kind, domain) 建二维索引，kind 是开放字符串，新增类型不改注册表；
//   - 按包原子装卸：unregisterPack 一次性撤销该包全部贡献，扩展之间互不牵连。
//
// 两条发现路径都汇入这里：
//   - 内置包：由 registerBuiltinPacks() 在启动期自注册；
//   - 外部插件：由插件扫描器加载后，把整个包 registerPack 进来。
//
// 所有解析器统一为 IMessageParser（kind=message）。旧 driver API 保留为兼容别名：
// 按 outputs() 含 FrameBatch 筛出数值批解析器，使 data.load 与既有测试零改动。
class ExtensionRegistry
{
public:
    // === 包级：装载/卸载的原子单位 ===
    bool registerPack(std::unique_ptr<IToolPack> pack);
    bool unregisterPack(const QString &packId);
    QStringList packIds() const;
    IToolPack *pack(const QString &packId) const;
    int packCount() const;

    // === 通用贡献查询（开放，支持任意 kind）===
    QList<IToolContribution *> contributions() const;
    QList<IToolContribution *> contributions(const QString &kind) const;
    QList<IToolContribution *> contributions(const QString &kind, Domain domain) const;
    QList<IToolContribution *> contributionsByDomain(Domain domain) const;
    IToolContribution *contribution(const QString &id) const;
    QStringList kinds() const;             // 当前已登记的所有 kind（去重）
    QStringList contributionIds() const;   // 所有贡献 id
    bool containsContribution(const QString &id) const;
    int contributionCount() const;

    // === 统一解析器视图（所有 kind=message 的 IMessageParser）===
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
    void rebuildIndexes();

    // CRUD 委托 libs 泛型表；所有权与顺序语义留在薄壳（Registry 不接管、不保序）。
    using PackTable = nextpilot::plotscope::registry::Registry<IToolPack *>;
    using ContributionTable = nextpilot::plotscope::registry::Registry<IToolContribution *>;
    using KindIndex = nextpilot::plotscope::registry::IndexedByKind<IToolContribution *>;

    std::vector<std::unique_ptr<IToolPack>> m_owned;         // 拥有所有包（间接拥有全部贡献）
    PackTable m_packIndex;                                    // pack id -> 包
    QStringList m_packOrder;                                  // 包登记顺序
    ContributionTable m_byId;                                 // 贡献 id -> 贡献
    KindIndex m_byKind;                                       // kind -> 贡献列表
    QStringList m_idOrder;                                    // 贡献登记顺序
};

} // namespace nextpilot::plotscope::registry
