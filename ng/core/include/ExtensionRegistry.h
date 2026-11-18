#pragma once

#include "IFormatDriver.h"

#include <QHash>
#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace ng::core
{

// 工具箱注册表：所有"能力驱动"的唯一登记处。
//
// 两条发现路径都汇入这里：
//   - 内置扩展（编译进 app）：由 registerBuiltinFormats() 在启动期自注册；
//   - 外部插件（动态库）：由宿主的插件扫描器加载后注册（本轮预留入口）。
//
// 注册表拥有 driver 的所有权；卸载 = 撤销登记 + 释放，
// 因此"卸掉一个格式"不会留下任何悬空的能力声明。
//
// 注意：注册表只装"能力"，不装器官。data/plot/script 这类有生命周期、
// 会上 UI 的功能仍是 IModule，由 ModuleHost 管理。
class ExtensionRegistry
{
public:
    // 登记一个驱动；同 id 已存在则拒绝（返回 false），不覆盖。
    bool registerDriver(std::unique_ptr<IFormatDriver> driver);

    // 撤销登记并释放驱动；返回是否确实移除。
    bool unregisterDriver(const QString &id);

    // 按 format 参数精确查找（如 "csv" / "ulog" / ".ulg"）。
    IFormatDriver *driverForFormat(const QString &format) const;

    // 按文件路径后缀查找（format 缺省时的兜底推断）。
    IFormatDriver *driverForPath(const QString &path) const;

    QList<IFormatDriver *> drivers() const;
    QStringList ids() const;
    bool contains(const QString &id) const;
    int count() const;

private:
    // unique_ptr 不可拷贝，Qt 6.2 的 QHash 不支持 move-only 值；
    // 故用 vector 持有所有权，QHash 只存裸指针做索引。
    std::vector<std::unique_ptr<IFormatDriver>> m_owned;
    QHash<QString, IFormatDriver *> m_index;
    QStringList m_order;
};

} // namespace ng::core
