#pragma once

#include "Intent.h"
#include "Result.h"
#include "IToolContribution.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ng::core
{

class DataRepository;

// 工具箱里"数据格式工具"的统一接口。
//
// 它与 IModule（器官）刻意分开：
//   - 器官有生命周期、可上 UI，参与宿主拓扑与意图抢占；
//   - 格式驱动没有 UI、没有独立生命周期，只是"能把某种文件读进 Dataset"的能力，
//     被动地被 ExtensionRegistry 按 format / 文件后缀查找、调用。
//
// 这样 extensions 是真正的工具箱：格式驱动只是其中一类工具，未来还能放算子、
// 导出器等其他工具门类，而不需要它们伪装成器官。
class IFormatDriver : public IToolContribution
{
public:
    virtual ~IFormatDriver() = default;

    // 格式驱动天然是 kind=format 的贡献；provider() 由基类持有、ToolPack 在 add 时写入。
    QString kind() const final { return ContributionKind::Format; }

    // 注册表内的唯一标识，如 "csv" / "ulog"（实现 IToolContribution）。
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // 该驱动所属的业务领域。工具箱按领域分类；默认通用，领域驱动应显式覆盖。
    FormatDomain domain() const override { return FormatDomain::General; }

    // 该驱动认识的文件后缀（不含点，小写），如 {"csv"} / {"ulg", "ulog"}。
    virtual QStringList suffixes() const = 0;

    // format 参数是否精确指向本驱动（如 "csv" / "ulog" / ".ulg"）。
    // 空 format 不在这里匹配——由注册表回退到按文件后缀推断。
    virtual bool acceptsFormat(const QString &format) const = 0;

    // 按文件路径后缀判断（通用实现，基于 suffixes()）。
    bool acceptsSuffix(const QString &path) const;

    // 旧路径：驱动自己读文件、自己建 Dataset、自己管缓存与入仓。
    //
    // **归并后（AGENTS.md §3 P0-1）内置格式都不再走这条路**：
    //   读字节、缓存、stable id 都归宿主（IngestOrchestrator），
    //   协议解析归 SPI 解析器，驱动只**声明** parserId。
    // 于是每种格式只有一份解析器实现，录制旁路只需一个 tee 就能覆盖所有格式。
    //
    // 保留本接口是为了给"尚未归并的格式"一个过渡出口：
    // 宿主在 parserId() 为空时才调用它（见 DataLoadHandler）。
    // 已归并的驱动应让本函数明确报错，而不是偷偷走另一条路
    // ——那会又长出第二条数据通路。
    //
    // 返回 DatasetId 作为 IntentResult.payload，与既有 data.load 契约一致。
    virtual Result<IntentResult> load(const Intent &intent, DataRepository &repo) = 0;

    // 本驱动对应的 **SPI 解析器 id**（如 "ulog"）；空串表示没有。
    //
    // 架构定位（AGENTS.md §3 P0-1，归并两条数据通路）：
    //   ng 曾有两条数据通路并存——
    //     A：data.load → IFormatDriver::load()，驱动自己读文件、自己建 Dataset；
    //     B：Source → IngestPipeline → parser SPI → Dataset，生产零调用。
    //   处置是**归并到 B，不新建第三条路**。
    //
    // 驱动在此的角色因此变成**声明**（"这个格式由哪个解析器负责"），
    // 而不是**执行**（自己去读文件）。宿主据此把字节喂进统一管道：
    //   读字节 -> 扇出给 N 个解析器 -> 各自产出数据集。
    // 于是：
    //   - 每加一个格式只写**一份**解析器实现；
    //   - 录制旁路只需一个 tee，就能覆盖所有格式；
    //   - 同一份字节可同时喂多个解析器（扇出）。
    //
    // 返回空串的驱动走旧的 load() 路径（尚未归并的格式仍可用）。
    virtual QString parserId() const { return QString(); }

    // 本格式的**表名策略**（声明，不是执行）。
    //
    // 为什么要它：SPI 解析器只认字节，不知道"这个文件叫什么"，
    // 于是**表名**（CSV 的表名原本取自 intent 参数或文件名）就成了宿主策略。
    // 但"哪些列是数值列、第一列是不是时间"是**格式知识**，归解析器；
    // "这张表叫什么"是**展示策略**，归驱动声明。
    //
    // 语义：返回本次加载应使用的表名；返回空串表示"沿用解析器给出的表名"。
    //   - CSV：intent.params["table"] 或文件名（与旧行为一致）；
    //   - ULog：空串（表名 = topic 名，由解析器给出）。
    //
    // 于是宿主在扇出绑定时按此设定**表名映射**，管道落库时套用——
    // 解析器保持"只认字节"，表名策略仍留在扩展层。
    virtual QString preferredTableName(const Intent &intent, const QString &sourcePath) const
    {
        Q_UNUSED(intent);
        Q_UNUSED(sourcePath);
        return QString();
    }

    // === 导入配置协议（声明式，供 GUI 通用对话框消费）===
    //
    // 为什么在这层：打开数据的"选项"（分隔符、表头、时间列…）是**格式知识**，
    // 只有格式驱动知道；但渲染对话框是 GUI 的事。于是驱动声明一份 schema，
    // GUI 通用渲染器按它生成控件——加一种格式只写 schema，不动对话框代码。
    // 这与老架构 DataSourceEntry.schema/probe 同源，但这里是纯数据接口，
    // 不携带任何 QWidget / 回调（headless 可用、可测试）。
    //
    // 三者都是默认空实现：没有选项的格式（如 ULog）不覆盖即可，
    // GUI 见到空 schema 就跳过配置页，直接加载——行为与过去一致。

    // 字段描述：{title, fields:[{key,type,label,default,options?,min?,max?...}],
    //            ui:{preview:{...}, warnings:[...]}}
    // type ∈ bool/int/double/enum/string/file。空对象 = 无选项。
    virtual QJsonObject importSchema() const { return QJsonObject(); }

    // 探测文件：读头部若干字节，返回 {delimiter,has_header,columns:[{name,kind}],
    //   preview:[[cell,...]], suggested_time, warnings:[...]}。
    // 供对话框预填字段、渲染预览与警告。失败返回 {"error": "..."}。
    // 只读取文件头，不物化全量（内存纪律）。
    virtual QJsonObject probeFile(const QString &path) const
    {
        Q_UNUSED(path);
        return QJsonObject();
    }

    // 依据探测结果生成默认选项 JSON（字符串）。空 = 用解析器内置默认。
    virtual QByteArray defaultImportOptions(const QString &path) const
    {
        Q_UNUSED(path);
        return QByteArray();
    }

    // 把导入对话框收集的选项转成**解析器 configure 真正要吃的 JSON**。
    //
    // 为什么需要它：有些格式的配置不是纯值，而是"指向另一个文件的配置"
    // （如自定义协议的 templatePath → 模板 JSON 文本）。解析器**不读文件**
    // （铁律），但读配置文件、把它展开成自包含参数是驱动（格式知识）的职责。
    // 默认原样返回——绝大多数格式（CSV/ULog）不需要转换。
    virtual QByteArray prepareParserOptions(const Intent &intent,
                                            const QByteArray &options) const
    {
        Q_UNUSED(intent);
        return options;
    }
};

} // namespace ng::core
