#include "WorkspaceModule.h"

#include "BuiltinModuleHub.h"
#include "ErrorCodes.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "IWorkspace.h"
#include "IWindow.h"
#include "ICommands.h"
#include "CommandAdapter.h"
#include "Intent.h"
#include "PersistentStateHub.h"
#include "SettingsStore.h"
#include "WorkspaceDataConfig.h"
#include "RuntimeHost.h"
#include "WorkspacePreset.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using nextpilot::plotscope::instantiation::InstantiationService;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

namespace
{

// 配置扁平映射：WorkspaceDataConfig 存在 SettingsStore 的 Workspace 作用域，
// 扁平 key = "ws/"+kSettingsKey（scopePrefix(Workspace)="ws"），值存 JSON 紧凑文本。
// 经 IWorkspace 的扁平 get/setConfiguration 读写，handler 不出现 SettingsStore 类型。
QString wsSettingsKey() { return QStringLiteral("ws/") + QString::fromLatin1(WorkspaceDataConfig::kSettingsKey); }

WorkspaceDataConfig loadWorkspaceData(const nextpilot::plotscope::workbench::IWorkspace &ws)
{
    const QVariant v = ws.getConfiguration(wsSettingsKey());
    if (v.typeId() == QMetaType::QString)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject())
            return WorkspaceDataConfig::fromJson(doc.object());
    }
    if (v.canConvert<QJsonObject>())
        return WorkspaceDataConfig::fromJson(v.toJsonObject());
    return WorkspaceDataConfig{};
}

bool saveWorkspaceData(nextpilot::plotscope::workbench::IWorkspace &ws, const WorkspaceDataConfig &cfg, QString *err)
{
    const QString text = QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson(QJsonDocument::Compact));
    ws.setConfiguration(wsSettingsKey(), QVariant(text));
    return ws.flushConfiguration(err);
}

// 预设目录：<AppData>/workspaces。放 AppData 而不是仓库目录，
// 因为预设属于用户产物，卸载/换版本不该被覆盖。
QString presetDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("workspaces"));
}

// 名字直接做文件名：只允许普通字符，避免 "../x" 之类越出目录。
bool isSafeName(const QString &name)
{
    if (name.isEmpty())
        return false;
    for (const QChar c : name)
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':'))
            return false;
    return !name.contains(QStringLiteral(".."));
}

QString presetPath(const QString &name)
{
    return QDir(presetDir()).filePath(name + QStringLiteral(".psw"));
}

bool writeJson(const QString &path, const QJsonObject &obj, QString *err)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err != nullptr)
            *err = QStringLiteral("无法写入 %1：%2").arg(path, f.errorString());
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

// 读文件并解析。失败时给出可执行的原因（不存在/非 JSON/版本过高）。
bool readPreset(const QString &path, WorkspacePreset *out, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err != nullptr)
            *err = QStringLiteral("无法读取 %1：%2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();
    if (doc.isNull() || !doc.isObject())
    {
        if (err != nullptr)
            *err = QStringLiteral("%1 不是合法的预设文件：%2").arg(path, parseError.errorString());
        return false;
    }
    QString versionError;
    WorkspacePreset preset = WorkspacePreset::fromJson(doc.object(), &versionError);
    if (!versionError.isEmpty())
    {
        if (err != nullptr)
            *err = versionError;
        return false;
    }
    if (preset.kind != QStringLiteral("preset"))
    {
        if (err != nullptr)
            *err = QStringLiteral("%1 不是 preset 类型（kind=%2）").arg(path, preset.kind);
        return false;
    }
    if (out != nullptr)
        *out = preset;
    return true;
}

} // namespace

// 预设意图处理器：workspace.preset.{save,open,openPath,list}
//
// 保存 = 抓当前源清单与画布布局；打开 = 回填设置后发恢复意图。
// 两条路都不做领域解释：源条目按原文回写，布局交给 plot.document 条目自己恢复。
class WorkspaceModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(nextpilot::plotscope::workbench::IWorkspace &ws, nextpilot::plotscope::workbench::IWindow &window,
                     nextpilot::plotscope::workbench::ICommands &cmds, QObject *parent)
        : QObject(parent), m_ws(ws), m_window(window), m_cmds(cmds)
    {
    }

    bool canHandle(const Intent &intent) const override
    {
        return intent.op.startsWith(QStringLiteral("workspace.preset."));
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const QString op = intent.op;
        if (op == QStringLiteral("workspace.preset.save"))
            return save(intent);
        if (op == QStringLiteral("workspace.preset.open"))
            return openByName(intent);
        if (op == QStringLiteral("workspace.preset.openPath"))
            return openByPath(intent);
        if (op == QStringLiteral("workspace.preset.list"))
            return list();
        return fail(err::Unsupported, QStringLiteral("未知预设操作: %1").arg(op));
    }

private:
    Result<IntentResult> save(const Intent &intent)
    {
        const QString name = intent.params.value(QStringLiteral("name")).toString().trimmed();
        if (!isSafeName(name))
            return fail(err::Args, QStringLiteral("预设名不合法：%1").arg(name));
        const bool generic = intent.params.value(QStringLiteral("generic")).toBool();

        WorkspacePreset preset;
        preset.name = name;
        preset.savedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        // 源清单取 settings 里的原文——保存不重新解释解析配置。
        const WorkspaceDataConfig cfg = loadWorkspaceData(m_ws);
        preset.sources = cfg.toJson().value(QStringLiteral("sources")).toArray();

        // 画布布局取 document 段里 plot.document 的产出（R3 登记的条目）。
        const QJsonObject doc = m_window.state().collect().value(QStringLiteral("document")).toObject();
        preset.plots = doc.value(QStringLiteral("plot.document")).toObject();

        if (generic)
            preset.makeGeneric();

        QString err;
        const QString path = presetPath(name);
        if (!writeJson(path, preset.toJson(), &err))
            return fail(err::Io, err);

        IntentResult r;
        r.payload = QVariantMap{{QStringLiteral("path"), path},
                                {QStringLiteral("sources"), preset.sources.size()},
                                {QStringLiteral("pages"),
                                 preset.plots.value(QStringLiteral("pages")).toArray().size()}};
        return r;
    }

    Result<IntentResult> openByName(const Intent &intent)
    {
        const QString name = intent.params.value(QStringLiteral("name")).toString().trimmed();
        if (!isSafeName(name))
            return fail(err::Args, QStringLiteral("预设名不合法：%1").arg(name));
        const QString path = presetPath(name);
        if (!QFileInfo::exists(path))
            return fail(err::NoEnt, QStringLiteral("预设不存在：%1").arg(name));
        return applyFile(path);
    }

    Result<IntentResult> openByPath(const Intent &intent)
    {
        const QString path = intent.params.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            return fail(err::Args, QStringLiteral("缺少 path"));
        return applyFile(path);
    }

    Result<IntentResult> list()
    {
        QJsonArray items;
        QDir dir(presetDir());
        const QStringList files = dir.entryList({QStringLiteral("*.psw")}, QDir::Files, QDir::Name);
        for (const QString &file : files)
        {
            QJsonObject item;
            item.insert(QStringLiteral("name"), QFileInfo(file).completeBaseName());
            WorkspacePreset preset;
            QString err;
            if (readPreset(dir.filePath(file), &preset, &err))
                item.insert(QStringLiteral("savedAt"), preset.savedAt),
                    item.insert(QStringLiteral("sourceBinding"), preset.sourceBinding);
            else
                item.insert(QStringLiteral("error"), err);
            items.append(item);
        }
        IntentResult r;
        r.payload = items;
        return r;
    }

    // 打开一份预设：先回填源清单，再发恢复意图，最后回填画布布局。
    // 顺序不能反：布局恢复要按 origin 找源，源没登记上就只能空态。
    Result<IntentResult> applyFile(const QString &path)
    {
        WorkspacePreset preset;
        QString err;
        if (!readPreset(path, &preset, &err))
            return fail(err::Args, err);

        QJsonObject sourcesDoc;
        sourcesDoc.insert(QStringLiteral("schemaVersion"), WorkspaceDataConfig::kSchemaVersion);
        sourcesDoc.insert(QStringLiteral("sources"), preset.sources);
        WorkspaceDataConfig cfg = WorkspaceDataConfig::fromJson(sourcesDoc);
        QString saveError;
        if (!saveWorkspaceData(m_ws, cfg, &saveError))
            return fail(err::Io, saveError);

        // generic 档只登记条目不自动导入：在没有对应解析器的机器上强制导入会失败。
        if (preset.sourceBinding == QStringLiteral("bound"))
        {
            const Result<QVariant> restored =
                m_cmds.executeCommand(QStringLiteral("data.workspace.restore"), QVariantMap{});
            if (!restored)
                return Result<IntentResult>(restored.error());
        }

        if (!preset.plots.isEmpty())
        {
            QJsonObject document;
            document.insert(QStringLiteral("plot.document"), preset.plots);
            m_window.state().restore(QJsonObject{{QStringLiteral("document"), document}});
        }

        IntentResult r;
        r.payload = QVariantMap{{QStringLiteral("name"), preset.name},
                                {QStringLiteral("sourceBinding"), preset.sourceBinding},
                                {QStringLiteral("sources"), preset.sources.size()},
                                {QStringLiteral("savedAt"), preset.savedAt}};
        return r;
    }

    nextpilot::plotscope::workbench::IWorkspace &m_ws;
    nextpilot::plotscope::workbench::IWindow &m_window;
    nextpilot::plotscope::workbench::ICommands &m_cmds;
};

ModuleInfo WorkspaceModule::moduleInfo()
{
    return ModuleInfo{QStringLiteral("mod.workspace"),
                      QStringLiteral("工作区预设模块"),
                      QStringLiteral("1.0"),
                      {QStringLiteral("workspace.preset")},
                      {}};
}

IWorkbenchContribution *WorkspaceModule::create() { return new WorkspaceModule(); }

ModuleInfo WorkspaceModule::info() const
{
    return moduleInfo();
}

void WorkspaceModule::initialize(InstantiationService &services)
{
    auto *ws = services.getService<nextpilot::plotscope::workbench::IWorkspace>(QStringLiteral("vscode.workspace"));
    auto *window = services.getService<nextpilot::plotscope::workbench::IWindow>(QStringLiteral("vscode.window"));
    auto *cmds = services.getService<nextpilot::plotscope::workbench::ICommands>(QStringLiteral("vscode.commands"));
    if (!ws || !window || !cmds)
        return;
    m_handler = new Handler(*ws, *window, *cmds, this);
    auto reg = [cmds](const QString &id, nextpilot::plotscope::intent::IIntentHandler *h) {
        cmds->registerCommand(id, nextpilot::plotscope::workbench::asCommandHandler(h, id));
    };
    reg(QStringLiteral("workspace.preset.save"), m_handler);
    reg(QStringLiteral("workspace.preset.open"), m_handler);
    reg(QStringLiteral("workspace.preset.openPath"), m_handler);
    reg(QStringLiteral("workspace.preset.list"), m_handler);
}

#include "WorkspaceModule.moc"

// 编译期自注册：新增模块无需改 App / 宿主。
PS_REGISTER_MODULE(WorkspaceModule)
