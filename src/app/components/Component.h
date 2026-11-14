#pragma once

#include <QJsonObject>
#include <Qt>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QDialog;
class QWidget;
struct PsPluginDescriptor;

namespace ps::components
{

struct ComponentManifest
{
    QString id;
    QString name;
    QString version;
    QStringList capabilities;
};

struct HostServices
{
    std::function<QWidget *()> window;
    std::function<void(const QString &)> log;
    std::function<QString()> configDir;
};

struct RibbonItem
{
    QString action;
    QString label;
    QString arg;
    bool large = true;
};

struct RibbonPanel
{
    QString title;
    QVector<RibbonItem> items;
    QString widget; // 宿主内建控件 ID：sourceGallery / plotGallery / layoutPicker / viewToggles
};

struct RibbonContribution
{
    QString page;
    QString panel;
    QVector<RibbonItem> items;
    QVector<RibbonPanel> panels;
    int order = 1000;
};

struct DataSourceSpec
{
    const PsPluginDescriptor *descriptor = nullptr;
    QString id;
    QString label;
    QStringList transports; // file / serial / tcp / udp / replay
};

struct DockContribution
{
    QString id;
    QString title;
    Qt::DockWidgetArea area = Qt::LeftDockWidgetArea;
    bool visible = true;
    int order = 0;
    std::function<QWidget *(QWidget *parent)> create; // 为空时由宿主按 id 构建内置面板
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
    void addAction(const QString &id, std::function<void(const ActionContext &)> handler);
    void addDataSource(const DataSourceSpec &source);
    void addDock(const DockContribution &dock);

    const ProtocolContribution *protocol(const QString &kind) const;
    const QVector<RibbonContribution> &ribbon() const { return m_ribbon; }
    const QVector<DataSourceSpec> &dataSources() const { return m_dataSources; }
    const QVector<DockContribution> &docks() const { return m_docks; }
    bool invoke(const QString &action, const ActionContext &ctx) const;

private:
    QVector<ProtocolContribution> m_protocols;
    QVector<RibbonContribution> m_ribbon;
    QVector<DataSourceSpec> m_dataSources;
    QVector<DockContribution> m_docks;
    QVector<QPair<QString, std::function<void(const ActionContext &)>>> m_actions;
};

class IComponent
{
public:
    virtual ~IComponent() = default;
    virtual ComponentManifest manifest() const = 0;
    virtual void contribute(Registry &registry, const HostServices &host) = 0;
};

} // namespace ps::components
