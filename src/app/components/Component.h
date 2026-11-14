#pragma once

#include <QJsonObject>
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
    bool large = true;
};

struct RibbonContribution
{
    QString page;
    QString panel;
    QVector<RibbonItem> items;
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
    void addLoader(const PsPluginDescriptor *descriptor);

    const ProtocolContribution *protocol(const QString &kind) const;
    const QVector<RibbonContribution> &ribbon() const { return m_ribbon; }
    const QVector<const PsPluginDescriptor *> &loaders() const { return m_loaders; }
    bool invoke(const QString &action, const ActionContext &ctx) const;

private:
    QVector<ProtocolContribution> m_protocols;
    QVector<RibbonContribution> m_ribbon;
    QVector<const PsPluginDescriptor *> m_loaders;
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
