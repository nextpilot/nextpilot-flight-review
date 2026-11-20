#pragma once

#include "SettingsStore.h"
#include "IngestPipeline.h"
#include "DatasetRegistry.h"
#include "ExtensionRegistry.h"
#include "Intent.h"
#include "SourceRegistry.h"

#include <QByteArray>

// 数据加载意图的统一入口（宿主的系统级能力，不归属任何可卸载模块）。
//
// 它不认识任何具体格式：拿到 data.load 意图后，
//   1) 有 format 参数 -> registry.driverForFormat 精确认领；
//   2) 否则 -> registry.driverForPath 按文件后缀推断；
//   3) 找到驱动 -> **按驱动声明的解析器走统一摄取管道**
//      （读字节 -> 扇出给解析器 -> 落库）；驱动没声明解析器则退回 driver.load()；
//   4) 找不到 -> eformat。
//
// 走管道的好处（AGENTS.md §3 P0-1）：
//   - 每种格式只写**一份**解析器实现（不再 driver + parser 各一份）；
//   - 读字节只有一处，录制旁路只需一个 tee 就能覆盖所有格式；
//   - 同一份字节可同时喂多个解析器（扇出）。
//
// 这样格式扩展之间彻底解耦：新增格式只往注册表加驱动；卸载=从注册表撤销，
// data 器官与其他器官都不用改。
//
// 成功后会把本次"来源 + 解析器"记入工作区配置（WorkspaceDataConfig，settings
// 的 ws/data/sources 键），供重启恢复。恢复重放时传 record=false，避免把恢复
// 出来的源又当新打开重复记录。
class DataLoadHandler final : public nextpilot::plotscope::intent::IIntentHandler
{
public:
    DataLoadHandler(nextpilot::plotscope::registry::ExtensionRegistry &registry,
                    nextpilot::plotscope::dataset::DatasetRegistry &repo,
                    nextpilot::plotscope::dataset::SourceRegistry &sources,
                    nextpilot::plotscope::context::IngestPipeline *pipeline = nullptr,
                    nextpilot::plotscope::context::SettingsStore *settings = nullptr)
        : m_registry(registry), m_repo(repo), m_sources(sources),
          m_pipeline(pipeline), m_settings(settings) {}

    bool canHandle(const nextpilot::plotscope::intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(
        const nextpilot::plotscope::intent::Intent &intent) override;

private:
    // 把本次打开记入工作区配置（来源/解析器/解析参数）。record=false 时跳过（恢复路径用）。
    void recordSpec(const QString &path, const QStringList &parserIds, bool record,
                    const QByteArray &parserOptions = QByteArray());

    nextpilot::plotscope::registry::ExtensionRegistry &m_registry;
    nextpilot::plotscope::dataset::DatasetRegistry &m_repo;
    nextpilot::plotscope::dataset::SourceRegistry &m_sources;
    // 摄取管道（宿主持有）。为空时 data.load 只走旧的 driver.load() 路径。
    nextpilot::plotscope::context::IngestPipeline *m_pipeline = nullptr;
    // 设置库（可选）：成功后把路径记入"最近文件"。为空则不记录。
    nextpilot::plotscope::context::SettingsStore *m_settings = nullptr;
};
