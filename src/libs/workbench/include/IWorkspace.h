#pragma once

// vscode.workspace 对位：5 个 access 的门面。服务 id "vscode.workspace"。
// 只暴露接口引用，不含任何具体 Hub 类型。

#include "IDatasetAccess.h"
#include "ISourceAccess.h"
#include "IIngest.h"
#include "IRecorder.h"
#include "IPlayback.h"
#include "Subscription.h"

#include <QVariant>
#include <QStringList>
#include <QVector>
#include <QPair>

// derived() 返回类型只前向声明，避免 include context 头（§1.3.1 要求头不依赖具体 Hub）。
// configuration 不返回 SettingsStore&（避免 workbench→context 反向依赖），改为扁平 QVariant
// 键值（§1.3.2 类型对齐表），底层由装配层映射到 SettingsStore::value/setValue。
namespace nextpilot::plotscope::context
{
class DerivedSeriesHub;
}

namespace nextpilot::plotscope::workbench
{

class IWorkspace
{
public:
    virtual ~IWorkspace() = default;

    virtual nextpilot::plotscope::dataset::IDatasetAccess &dataset() = 0;
    virtual nextpilot::plotscope::dataset::ISourceAccess &sources() = 0;
    virtual nextpilot::plotscope::dataset::IIngest &ingest() = 0;
    virtual nextpilot::plotscope::recording::IRecorder &recorder() = 0;
    virtual nextpilot::plotscope::recording::IPlayback &playback() = 0;

    // vscode.workspace.getConfiguration 的扁平键值面（底层映射 SettingsStore::value/setValue，
    // 但不暴露 SettingsStore 类型，避免 libs/workbench 反向依赖 libs/context）。
    virtual QVariant getConfiguration(const QString &key, const QVariant &def = {}) const = 0;
    virtual void setConfiguration(const QString &key, const QVariant &value) = 0;
    // 把内存中的配置原子写盘（对应 SettingsStore::flush）。扁平 get/set 之外唯一必需的
    // 能力：analysis/workspace handler 每次写收藏/最近/工作区配置后立即持久化。
    virtual bool flushConfiguration(QString *err = nullptr) = 0;
    // 删键 / 按前缀枚举键（settings.remove / settings.list 用；scope 名映射为 "app/" "ws/" "doc/" 前缀）。
    virtual void removeConfiguration(const QString &key) = 0;
    virtual QStringList configurationKeys(const QString &prefix = {}) const = 0;
    // 格式驱动目录：{id, displayName} 元数据对（按名给能力，不暴露 ToolboxHub/IMessageParser）。
    // DataModule 的 onAttach 用它生成"来源"Ribbon 图块，避免模块回头拿宿主拿 drivers。
    virtual QVector<QPair<QString, QString>> sourceDriverMetas() const = 0;

    // 派生序列注册表（libs 具体类型，装配层注入）。
    virtual nextpilot::plotscope::context::DerivedSeriesHub &derived() = 0;
};

} // namespace nextpilot::plotscope::workbench
