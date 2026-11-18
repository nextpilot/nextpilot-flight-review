#pragma once

#include "IToolContribution.h"
#include "IToolPack.h"
#include "IFormatDriver.h"
#include "FormatDomain.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace ng::core
{

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
// 保留 registerDriver / driverForFormat / driverForPath / drivers 等旧 API（内部
// 把单个驱动包装成一个隐式包），使 data.load 与既有调用零改动。
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
    QList<IToolContribution *> contributions(const QString &kind, FormatDomain domain) const;
    QList<IToolContribution *> contributionsByDomain(FormatDomain domain) const;
    IToolContribution *contribution(const QString &id) const;
    QStringList kinds() const;             // 当前已登记的所有 kind（去重）
    QStringList contributionIds() const;   // 所有贡献 id
    bool containsContribution(const QString &id) const;
    int contributionCount() const;

    // === 格式快捷 API（向后兼容 data.load 与既有测试）===
    bool registerDriver(std::unique_ptr<IFormatDriver> driver);
    bool unregisterDriver(const QString &id);
    IFormatDriver *driverForFormat(const QString &format) const;
    IFormatDriver *driverForPath(const QString &path) const;
    QList<IFormatDriver *> drivers() const;
    QList<IFormatDriver *> driversByDomain(FormatDomain domain) const;
    QList<FormatDomain> domains() const; // 已有格式驱动的领域（去重）
    QStringList ids() const;             // 格式驱动 id
    bool contains(const QString &id) const;
    int count() const;                   // 格式驱动数量

private:
    void rebuildIndexes();

    std::vector<std::unique_ptr<IToolPack>> m_owned;         // 拥有所有包（间接拥有全部贡献）
    QHash<QString, IToolPack *> m_packIndex;                  // pack id -> 包
    QStringList m_packOrder;
    QHash<QString, IToolContribution *> m_byId;               // 贡献 id -> 贡献
    QHash<QString, QList<IToolContribution *>> m_byKind;      // kind -> 贡献列表
    QStringList m_idOrder;                                    // 贡献登记顺序
};

} // namespace ng::core
