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

#include <functional>
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
    using PackFactory = std::function<std::unique_ptr<IToolPack>()>;

    // === 内置包静态自注册通道（原 BuiltinPackRegistry，折叠进容器本体）===
    //
    // 内置包与插件包语义相同（都是 IToolPack），唯一差别是"怎么被发现"：
    // 插件由扫描器加载后 registerPack；内置包在各自 TU 静态初始化期把**工厂**
    // 登记进下面的进程级静态表（NG_REGISTER_PACK 宏），宿主启动时
    // installBuiltinPacks() 统一灌进实例。于是"新增一个内置包"= 新增目录 +
    // 一行 CMake，宿主/Toolbox/既有包代码零改动。
    //
    // 静态表只存工厂（无所有权、装满只读、进程生命周期）；实例表拥有包。
    // 二者生命周期不同，故静态方法与实例方法并存于本类。
    static bool registerBuiltinPack(const QString &id, PackFactory factory);
    QStringList installBuiltinPacks();        // 装进本实例，返回成功的包 id
    static void resetBuiltinPacks();          // 清空静态表（仅测试用）

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

// 注册宏：在包 TU 文件末尾使用，把本包工厂登记进静态自注册表。
//
// 用法：
//   NG_REGISTER_PACK(nextpilot::plotscope::ext::AutopilotPack)
//
// 其中 AutopilotPack 需提供：
//   static QString packId();
//   static std::unique_ptr<nextpilot::plotscope::registry::IToolPack> build();
//
// 宏必须带完整命名空间：它在调用处的命名空间作用域展开。
#define NG_REGISTER_PACK(FQN)                                                             \
    namespace {                                                                           \
    const bool g_ng_pack_reg_ =                                                           \
        ::nextpilot::plotscope::registry::ExtensionRegistry::registerBuiltinPack(         \
            FQN::packId(),                                                                \
            []() -> std::unique_ptr<::nextpilot::plotscope::registry::IToolPack> { return FQN::build(); }); \
    }
