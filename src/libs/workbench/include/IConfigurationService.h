#pragma once

// 配置读回与分层合并——VSCode 对位
// `vs/platform/configuration/common/configuration.ts` 的 IConfigurationService。
//
// 职责边界：
//   - 声明面（contributes.configuration，第 1 笔进 ToolboxHub）与持久化
//     （SettingsStore，app 唯一出口）之间的唯一合并 / 复位 / 诊断出口。
//   - 分层合并按既有 SettingsScope 三层：user(App) ← workspace ← document，
//     即 document 覆盖 workspace 覆盖 user 覆盖声明默认值。
//   - 持久化只走 SettingsStore（键空间 "config/<点号键>"，按 scope 分层），
//     不造第二套存储；owner 不读写任何配置文件（§5）。
//   - owner unmount：其属性从目录消失；各层残留值保留但不生效 + 诊断（不删用户数据）。
//   - 实现放 assembly（ConfigurationServiceAdapter）；壳经本接口读写，不 include registry。

#include "ContributionParser.h" // ConfigurationSpec（纯数据）

#include <QJsonValue>
#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench {

// 单层来源（inspect 用）：value 为 Undefined 表示该层未显式设置。
struct ConfigurationLayer
{
    QJsonValue value;
    bool set = false;
};

// 键的全景视图：每层来源 + 声明默认值 + 合并结果 + 声明者。
struct ConfigurationInspect
{
    QString key;
    bool declared = false;        // 是否仍在某 owner 的 configuration 声明中
    QString declaredOwner;        // 声明者（未声明为空串）
    QString scope;                // 声明的 VSCode ConfigurationScope（决定哪些层参与合并）
    ConfigurationLayer user;      // SettingsScope::App
    ConfigurationLayer workspace; // SettingsScope::Workspace
    ConfigurationLayer document;  // SettingsScope::Document
    QJsonValue defaultValue;      // 声明默认值（未声明 = Undefined）
    QJsonValue effective;         // scope 允许的最外层 ← … ← user ← default
};

class IConfigurationService
{
public:
    virtual ~IConfigurationService() = default;

    // 设置目录（声明面读回，跨 owner；目录排序 (order, title) 归壳/渲染层）。
    virtual QList<nextpilot::plotscope::registry::ConfigurationSpec> configurations() const = 0;

    // 分层写入：value 为 Undefined 即清除该层覆盖。只落 SettingsStore，别无出口；
    // 未声明键允许写入（残留值保留但不生效 + 诊断）。
    virtual void setUserValue(const QString &key, const QJsonValue &value) = 0;
    virtual void setWorkspaceValue(const QString &key, const QJsonValue &value) = 0;
    virtual void setDocumentValue(const QString &key, const QJsonValue &value) = 0;

    // 合并读：document ← workspace ← user ← 声明默认值；未声明且无残留 → Undefined。
    virtual QJsonValue effectiveValue(const QString &key) const = 0;

    // 每层来源全景（设置页"哪层覆盖了谁"的数据源）。
    virtual ConfigurationInspect inspect(const QString &key) const = 0;

    // 单键复位：清 user/workspace/document 三层覆盖（默认值是声明数据，不可写）。
    virtual void reset(const QString &key) = 0;

    // 诊断：键未在任何 owner 的 configuration 声明中但某层有残留值 →
    // 「残留值保留但不生效」。纯状态推导（读时算），不依赖卸载时序。
    struct Diagnostic
    {
        QString key;
        QString message;
    };
    virtual QList<Diagnostic> diagnostics() const = 0;
};

} // namespace nextpilot::plotscope::workbench
