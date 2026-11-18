#pragma once

#include "IUiContribution.h"
#include "IPanelContribution.h"
#include "IScriptContribution.h"
#include "IDataContribution.h"
#include "IExampleContribution.h"
#include "IDocContribution.h"

#include <QList>
#include <QString>

// 内置贡献的通用数据持有实现。
//
// 这些类不含任何业务逻辑：只是把"身份 + 数据"装进 typed 接口，
// 供知识包在组装期声明式地 add<T>(...)。领域专属的复杂行为（比如真正解析脚本、
// 生成面板控件）以后可以派生出专用类替换，注册表与宿主完全无感。
namespace ng::ext
{

class GenericUiContribution final : public ng::core::IUiContribution
{
public:
    GenericUiContribution(QString id, QString name, ng::core::FormatDomain domain,
                          QList<ng::core::UiAction> actions);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QList<ng::core::UiAction> actions() const override { return m_actions; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QList<ng::core::UiAction> m_actions;
};

// 领域面板贡献的通用数据持有实现。
//
// 与 GenericUiContribution 同构：只装"身份 + 数据"，不含任何控件逻辑。
// 面板的**实现**在知识包的呈现层（GUI 侧）注册进 PanelFactoryRegistry，
// 与这里的**描述**用同一个 id 关联。
class GenericPanelContribution final : public ng::core::IPanelContribution
{
public:
    GenericPanelContribution(QString id, QString name, ng::core::FormatDomain domain,
                            QList<ng::core::PanelSpec> panels);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QList<ng::core::PanelSpec> panels() const override { return m_panels; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QList<ng::core::PanelSpec> m_panels;
};

class GenericScriptContribution final : public ng::core::IScriptContribution
{
public:
    GenericScriptContribution(QString id, QString name, ng::core::FormatDomain domain,
                              QString language, QString source, QStringList exports);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QString language() const override { return m_language; }
    QString source() const override { return m_source; }
    QStringList exports() const override { return m_exports; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QString m_language;
    QString m_source;
    QStringList m_exports;
};

class GenericDataContribution final : public ng::core::IDataContribution
{
public:
    GenericDataContribution(QString id, QString name, ng::core::FormatDomain domain,
                            QList<ng::core::DataAsset> assets);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QList<ng::core::DataAsset> assets() const override { return m_assets; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QList<ng::core::DataAsset> m_assets;
};

class GenericExampleContribution final : public ng::core::IExampleContribution
{
public:
    GenericExampleContribution(QString id, QString name, ng::core::FormatDomain domain,
                               QList<ng::core::ExampleEntry> entries);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QList<ng::core::ExampleEntry> entries() const override { return m_entries; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QList<ng::core::ExampleEntry> m_entries;
};

class GenericDocContribution final : public ng::core::IDocContribution
{
public:
    GenericDocContribution(QString id, QString name, ng::core::FormatDomain domain,
                           QList<ng::core::DocEntry> entries);
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    ng::core::FormatDomain domain() const override { return m_domain; }
    QList<ng::core::DocEntry> entries() const override { return m_entries; }

private:
    QString m_id;
    QString m_name;
    ng::core::FormatDomain m_domain;
    QList<ng::core::DocEntry> m_entries;
};

} // namespace ng::ext
