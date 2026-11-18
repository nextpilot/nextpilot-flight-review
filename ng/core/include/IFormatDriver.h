#pragma once

#include "Intent.h"
#include "Result.h"
#include "IToolContribution.h"

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

    // 执行加载：把 intent（含 path/table/id/nocache 等）解析进 Dataset 并入仓库。
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
};

} // namespace ng::core
