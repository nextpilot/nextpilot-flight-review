#include "BuiltinContributions.h"

using namespace ng::core;

namespace ng::ext
{

GenericUiContribution::GenericUiContribution(QString id, QString name, FormatDomain domain,
                                             QList<UiAction> actions)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_actions(std::move(actions)) {}

GenericPanelContribution::GenericPanelContribution(QString id, QString name, FormatDomain domain,
                                                 QList<PanelSpec> panels)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_panels(std::move(panels)) {}

GenericScriptContribution::GenericScriptContribution(QString id, QString name, FormatDomain domain,
                                                     QString language, QString source, QStringList exports)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_language(std::move(language)),
      m_source(std::move(source)), m_exports(std::move(exports)) {}

GenericDataContribution::GenericDataContribution(QString id, QString name, FormatDomain domain,
                                                 QList<DataAsset> assets)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_assets(std::move(assets)) {}

GenericExampleContribution::GenericExampleContribution(QString id, QString name, FormatDomain domain,
                                                       QList<ExampleEntry> entries)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_entries(std::move(entries)) {}

GenericDocContribution::GenericDocContribution(QString id, QString name, FormatDomain domain,
                                               QList<DocEntry> entries)
    : m_id(std::move(id)), m_name(std::move(name)), m_domain(domain), m_entries(std::move(entries)) {}

} // namespace ng::ext
