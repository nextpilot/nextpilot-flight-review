#pragma once

#include <QJsonObject>
#include <QVariantMap>

#include "../../modules/runtime/include/ScriptService.h"
#include <Qt>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

class QDialog;
class QWidget;
class QPlainTextEdit;
class QComboBox;
class QTableWidget;
struct PsExtensionDescriptor;

namespace ps::components
{

struct ComponentManifest
{
    QString id;
    QString name;
    QString version;
    QStringList capabilities;
};

struct HostContext
{
    // 宿主注入给组件的运行时上下文（窗口 / 日志 / 配置目录）。
    // 与 HostApi 区分：HostApi 是扩展回调宿主的 C ABI 函数表。
    std::function<QWidget *()> window;
    std::function<void(const QString &)> log;
    std::function<QString()> configDir;
    std::function<void(double)> setPlayhead;
    std::function<bool(const QString &, const QString &)> invoke;
    std::function<void(QWidget *, QPlainTextEdit *, QComboBox *[4], QTableWidget *)> bindScriptPanel;
};

struct RibbonItem
{
    QString action;
    QString label;
    QString arg;
    bool large = true;
    QString icon; // 资源名，不带 :/icons/ 前缀
};

struct RibbonPanel
{
    QString title;
    QVector<RibbonItem> items;
    std::function<void(QWidget *)> build; // 模块自建面板；参数是 SARibbonPanel*
};

struct RibbonContribution
{
    QString page;
    QString panel;
    QVector<RibbonItem> items;
    QVector<RibbonPanel> panels;
    int order = 1000;
};

struct StatusChipContribution
{
    QString id;
    QString text;
    int order = 0;
};

struct DataSourceSpec
{
    const PsExtensionDescriptor *descriptor = nullptr;
    QString id;
    QString label;
    QStringList transports; // file / serial / tcp / udp / replay
    std::function<QDialog *(const QString &path, QWidget *parent)> editor; // 可选配置编辑器
};

struct DockContribution
{
    QString id;
    QString title;
    Qt::DockWidgetArea area = Qt::LeftDockWidgetArea;
    bool visible = true;
    int order = 0;
    QString icon; // 资源路径，空时由宿主给默认图标
    // 返回 QDockWidget 或内容控件；为空表示该贡献不可构造。
    std::function<QWidget *(QWidget *parent)> create;
    std::function<void(QWidget *parent)> layout; // 所有 Dock 创建完成后调用，模块自己排布
};

// 通用业务对话框：宿主只按 id 创建，不认识具体对话框类型。
struct DialogContribution
{
    QString id;
    std::function<QDialog *(const QVariantMap &args, QWidget *parent)> create;
};

struct SettingsContribution
{
    QString id;
    QString title;
    QString icon; // 资源路径，可为空
    int order = 0;
    // 返回设置页控件；参数是 MainWindow，页控件由设置对话框接管。
    std::function<QWidget *(QWidget *host)> create;
    // 对话框点 OK / Apply 时调用，page 为 create 返回的控件。
    std::function<void(QWidget *page)> apply;
};

struct ActionContext
{
    QWidget *parent = nullptr;
};

struct ProtocolContribution
{
    QString kind;
    QString label;
    QJsonObject schema;
    std::function<QStringList(const QJsonObject &)> validate;
    std::function<QString()> blank;
    std::function<QDialog *(const QString &, QWidget *)> createEditor;
};

class Registry
{
public:
    void addProtocol(const ProtocolContribution &protocol);
    void addRibbon(const RibbonContribution &ribbon);
    void addStatusChip(const StatusChipContribution &chip) { m_statusChips.append(chip); }
    void addAction(const QString &id, std::function<void(const ActionContext &)> handler);
    void addDataSource(const DataSourceSpec &source);
    void addDock(const DockContribution &dock);
    void addDialog(const DialogContribution &dialog);
    void addSettings(const SettingsContribution &settings);
    void addScriptRunner(const ScriptRunnerContribution &runner);
    void addWindowHook(std::function<void(QWidget *)> hook) { m_windowHooks.append(std::move(hook)); }

    const ProtocolContribution *protocol(const QString &kind) const;
    const QVector<RibbonContribution> &ribbon() const { return m_ribbon; }
    const QVector<StatusChipContribution> &statusChips() const { return m_statusChips; }
    const QVector<DataSourceSpec> &dataSources() const { return m_dataSources; }
    const QVector<DockContribution> &docks() const { return m_docks; }
    const QVector<SettingsContribution> &settings() const { return m_settings; }
    const QVector<std::function<void(QWidget *)>> &windowHooks() const { return m_windowHooks; }
    bool invoke(const QString &action, const ActionContext &ctx) const;
    QDialog *createDialog(const QString &id, const QVariantMap &args, QWidget *parent) const;
    ScriptResult runScript(const QString &source, const QVector<ScriptSeries> &inputs,
                           const QMap<QString, double> &params = {},
                           const QString &language = QString()) const;

private:
    QVector<ProtocolContribution> m_protocols;
    QVector<RibbonContribution> m_ribbon;
    QVector<StatusChipContribution> m_statusChips;
    QVector<DataSourceSpec> m_dataSources;
    QVector<DockContribution> m_docks;
    QVector<DialogContribution> m_dialogs;
    QVector<SettingsContribution> m_settings;
    ScriptRunnerContribution m_scriptRunner;
    QVector<QPair<QString, std::function<void(const ActionContext &)>>> m_actions;
    QVector<std::function<void(QWidget *)>> m_windowHooks;
};

class IComponent
{
public:
    virtual ~IComponent() = default;
    virtual ComponentManifest manifest() const = 0;
    virtual void contribute(Registry &registry, HostContext &host) = 0;
};

enum class ExtensionType
{
    Builtin,
    Dynamic
};

struct ExtensionDescriptor
{
    QString id;
    QString name;
    QString version;
    ExtensionType type = ExtensionType::Builtin;

    // 仅 Dynamic 扩展使用
    QString libraryPath;
    QString manifestJson;
    int abiVersion = 0;
    const PsExtensionDescriptor *descriptor = nullptr;
};

using ExtensionFactory = std::function<std::unique_ptr<IComponent>()>;

struct ExtensionEntry
{
    ExtensionDescriptor descriptor;
    ExtensionFactory factory;
};

class ExtensionRegistry
{
public:
    static ExtensionRegistry &instance();

    bool addExtension(const ExtensionDescriptor &descriptor, ExtensionFactory factory);
    const QVector<ExtensionEntry> &extensions() const { return m_extensions; }

private:
    ExtensionRegistry() = default;
    QVector<ExtensionEntry> m_extensions;
};

} // namespace ps::components
