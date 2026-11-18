#include "UlogExtension.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "Intent.h"
#include "UlogStreamParser.h"

#include <QFileInfo>

using namespace ng::core;
using namespace ng::host;

// 意图处理器：识别 "data.load" 且 format=ulog/.ulg 的意图，流式解析产出 Dataset。
//
// 与 CsvExtension 完全同构的生命周期：缓存优先 -> 未命中则流式解析 -> 写缓存 -> 入仓库。
// 区别只在解析内核：CSV 逐行、ULog 逐段（复用旧 extension 的 ULog 协议算法），
// 但两者的产出端都是 Dataset::appendFrame 分块灌入 Rust，C++ 侧零全量副本。
class UlogExtension::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(DataRepository &repo, QObject *parent)
        : QObject(parent), m_repo(repo) {}

    bool canHandle(const Intent &intent) const override
    {
        if (intent.op != QStringLiteral("data.load"))
            return false;
        const QString fmt = intent.params.value(QStringLiteral("format")).toString();
        return fmt == QStringLiteral("ulog") || fmt == QStringLiteral(".ulg");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const QString path = intent.params.value(QStringLiteral("path")).toString();
        const QString fmt = intent.params.value(QStringLiteral("format"), QStringLiteral("ulog")).toString();
        if (fmt != QStringLiteral("ulog") && fmt != QStringLiteral(".ulg"))
            return fail(QStringLiteral("eformat"), QStringLiteral("非 ULog 格式: %1").arg(fmt));

        const QFileInfo fi(path);
        if (!fi.exists())
            return fail(QStringLiteral("eio"), QStringLiteral("文件不存在: %1").arg(path));

        const qint64 sourceSize = fi.size();
        const qint64 sourceMtime = fi.lastModified().toMSecsSinceEpoch();
        const QString cachePath = path + QStringLiteral(".psc");

        Dataset ds;
        bool fromCache = false;

        // 1) 缓存优先：命中则直接还原 Rust 数据集，连解析都跳过。
        if (!intent.params.value(QStringLiteral("nocache"), false).toBool())
        {
            QString cacheErr;
            if (auto cached = Dataset::loadCache(cachePath, sourceSize, sourceMtime, &cacheErr))
            {
                ds = std::move(*cached);
                ds.setFormat(QStringLiteral("ulog"));
                fromCache = true;
            }
        }

        // 2) 未命中：流式解析 ULog -> appendFrame 灌入 Rust typed 表。
        if (!fromCache)
        {
            ds = Dataset::create(fi.completeBaseName(), QStringLiteral("ulog"));
            QString parseErr;
            if (!ng::ext::ulog::parseUlogStream(path, ds, parseErr))
                return fail(QStringLiteral("eformat"),
                            QStringLiteral("ULog 解析失败: %1").arg(parseErr));
            // 所有批次灌完后刷新一次 schema 投影（O(列数)；不能在每批后刷，否则 O(批×行)）。
            ds.refreshSchema();

            // 3) 写缓存（按源文件指纹，失败不影响加载成功）。
            QString cacheErr;
            ds.saveCache(cachePath, sourceSize, sourceMtime, &cacheErr);
        }

        // 意图可携带稳定 id（录制/回放用）；未指定则用仓库自增 id。
        DatasetId id;
        if (intent.params.contains(QStringLiteral("id")))
        {
            const DatasetId wanted = intent.params.value(QStringLiteral("id")).toInt();
            Result<DatasetId> added = m_repo.addWithStableId(std::move(ds), wanted);
            if (!added)
                return fail(added.error().code, added.error().message);
            id = added.value();
        }
        else
        {
            id = m_repo.add(std::move(ds));
        }
        IntentResult r;
        r.payload = id;
        return r;
    }

private:
    DataRepository &m_repo;
};

ModuleInfo UlogExtension::info() const
{
    return ModuleInfo{
        QStringLiteral("ext.ulog"),
        QStringLiteral("ULog 格式扩展"),
        QStringLiteral("1.0"),
        ModuleKind::Extension,
        {QStringLiteral("format.ulog")},
        {}
    };
}

void UlogExtension::initialize(ModuleContext &ctx)
{
    m_handler = new Handler(ctx.data(), this); // parent=this，所有权即撤销
    ctx.registerIntentHandler(m_handler);
}

#include "UlogExtension.moc"
