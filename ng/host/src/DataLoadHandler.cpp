#include "DataLoadHandler.h"
#include "ErrorCodes.h"

#include "AppContext.h"
#include "DataCommandsHandler.h"
#include "DatasetRegistry.h"
#include "IngestOrchestrator.h"
#include "IngestPipeline.h"
#include "SettingsStore.h"
#include "WorkspaceDataConfig.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

bool DataLoadHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("data.load");
}

Result<IntentResult> DataLoadHandler::execute(const Intent &intent)
{
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    const QString format = intent.params.value(QStringLiteral("format")).toString();
    // 恢复重放时传 false：不要把恢复出来的源又当成"新打开"记一遍。
    const bool record = intent.params.value(QStringLiteral("record"), true).toBool();

    IMessageParser *driver = format.isEmpty()
        ? m_registry.driverForPath(path)
        : m_registry.driverForFormat(format);

    // format 显式但按后缀能兜底时再试一次（容忍 format 拼写与后缀不一致）。
    if (!driver && !format.isEmpty())
        driver = m_registry.driverForPath(path);

    if (!driver)
        return fail(err::Format,
                    QStringLiteral("没有已注册的格式驱动能处理: %1 (format=%2)")
                        .arg(path, format));

    // --- 走统一摄取管道（归并后的主路径）---
    //
    // 驱动只**声明**"这个格式由哪个解析器负责"（parserId()），
    // 读字节与扇出都由宿主编排——于是每种格式只有一份解析器实现，
    // 且录制旁路只需一个 tee 就能覆盖所有格式。
    const QString parserId = driver->parserId();
    if (m_pipeline && !parserId.isEmpty())
    {
        // 宿主策略（三项都不是解析器的事）：
        //   · 表名：解析器只认字节，给不出"这张表叫什么" —— 由驱动声明；
        //   · 缓存：按源文件指纹复用已解析的数据集；
        //   · 稳定 id：录制/回放时数据集 id 不能漂移。
        IngestOrchestrator::Options options;

        // 表名：驱动的 preferredTableName 给出目标表名（单表格式才声明）。
        // ULog 这类多表格式返回空串 —— 表名由 topic 决定，宿主不插手。
        options.tableName = driver->preferredTableName(intent, path);

        // 缓存：默认开启（与旧 driver 行为一致），nocache=true 时关闭。
        options.useCache = !intent.params.value(QStringLiteral("nocache"), false).toBool();

        // 稳定 id：脚本可显式指定，录制/回放据此保持 id 一致。
        if (intent.params.contains(QStringLiteral("id")))
            options.stableId = intent.params.value(QStringLiteral("id")).toInt();

        // 解析参数：导入对话框收集的选项，经 intent 透传给解析器 configure。
        // 允许 JSON 字符串或 QVariantMap（脚本场景）。
        QByteArray parserOptions;
        const QVariant ov = intent.params.value(QStringLiteral("options"));
        if (ov.type() == QVariant::String)
            parserOptions = ov.toString().toUtf8();
        else if (ov.isValid() && !ov.toMap().isEmpty())
            parserOptions = QJsonDocument(QJsonObject::fromVariantMap(ov.toMap())).toJson(QJsonDocument::Compact);
        options.parserOptions = parserOptions;
        // 驱动可把"指向配置文件的选项"展开成解析器能直接用的自包含参数
        // （如自定义协议 templatePath → template 文本；解析器不读文件）。
        // 展开结果只喂本次解析；申请单仍记**原始** options（templatePath），
        // 于是重放时重新读模板文件，而不是把整篇 JSON 塞进 settings。
        const QByteArray prepared = driver->prepareParserOptions(intent, parserOptions);
        options.parserOptions = prepared;

        IngestOrchestrator orchestrator(m_repo, m_sources, *m_pipeline);
        const Result<int> opened = orchestrator.openFile(path, QStringList{parserId},
                                                        QString(), options);
        if (!opened)
            return fail(opened.error().code, opened.error().message);

        // 成功载入 → 记入最近文件（settings 持久化，CLI 也能读）。
        if (m_settings)
        {
            QStringList files = m_settings->value(QLatin1String(DataCommandsHandler::kRecentKey)).toStringList();
            files.removeAll(path);
            files.prepend(path);
            if (files.size() > 10)
                files = files.mid(0, 10);
            m_settings->setValue(QLatin1String(DataCommandsHandler::kRecentKey), files);
        }
        // 记入工作区配置（与"最近文件"并列：一个是偏好，一个是可恢复的申请单）。
        recordSpec(path, QStringList{parserId}, record, parserOptions);

        IntentResult r;
        r.payload = opened.value();
        return r;
    }

    // 该解析器声明了 FrameBatch 输出却没有 parserId，或摄取管道未注入——配置错误。
    return fail(err::NoParser,
                QStringLiteral("解析器 %1 未提供可执行的 parserId，无法走摄取管道")
                    .arg(driver->id()));
}

void DataLoadHandler::recordSpec(const QString &path, const QStringList &parserIds, bool record,
                                 const QByteArray &parserOptions)
{
    if (!record || !m_settings || path.isEmpty())
        return;
    WorkspaceDataConfig cfg = WorkspaceDataConfig::load(*m_settings);
    SourceSpec spec;
    spec.origin = path;
    spec.kind = QStringLiteral("file");
    spec.parserIds = parserIds;
    // 解析参数按 parserId 分组存入申请单，恢复时原样重放。
    if (!parserOptions.isEmpty() && !parserIds.isEmpty())
    {
        QJsonObject params = QJsonDocument::fromJson(parserOptions).object();
        if (!params.isEmpty())
            spec.parserParams.insert(parserIds.first(), params);
    }
    cfg.upsert(spec);
    cfg.save(*m_settings);
}
