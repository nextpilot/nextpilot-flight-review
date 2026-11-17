#pragma once

#include "Component.h"
#include <QPair>
#include <QString>
#include <QVector>
#include "TimeBookmark.h"

namespace ps::runtime { struct ModuleContext; }
struct PsChannelInfo;
namespace ps::data_source { struct DataSource; }
namespace ps::components {
const QVector<const PsChannelInfo *> &dataSourceChannels(ps::runtime::ModuleContext *ctx);
int dataSourceActiveId(ps::runtime::ModuleContext *ctx);
QString dataSourceActivePath(ps::runtime::ModuleContext *ctx);
QString dataSourceActiveFormat(ps::runtime::ModuleContext *ctx);
QVector<ps::TimeBookmark> &dataSourceTimeMarkers(ps::runtime::ModuleContext *ctx);
void addDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, double t, const QString &label, bool prompt);
void removeDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id);
void moveDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, double to);
void renameDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, const QString &label);
}

namespace ps::components
{

// 数据源模块：注册数据源 Dock 与“数据”Ribbon页。
void registerDataSourceContributions(Registry &registry);

class DataSourceComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components

