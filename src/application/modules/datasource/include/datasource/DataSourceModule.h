#pragma once

#include "Component.h"
#include <QPair>
#include <QString>
#include <QVector>
#include "TimeBookmark.h"

class MainWindow;
struct PsChannelInfo;
namespace ps::data_source { struct DataSource; }
namespace ps::components {
const QVector<const PsChannelInfo *> &dataSourceChannels(MainWindow *mw);
int dataSourceActiveId(MainWindow *mw);
QString dataSourceActivePath(MainWindow *mw);
QString dataSourceActiveFormat(MainWindow *mw);
QVector<ps::TimeBookmark> &dataSourceTimeMarkers(MainWindow *mw);
void addDataSourceTimeMarker(MainWindow *mw, double t, const QString &label, bool prompt);
void removeDataSourceTimeMarker(MainWindow *mw, const QString &id);
void moveDataSourceTimeMarker(MainWindow *mw, const QString &id, double to);
void renameDataSourceTimeMarker(MainWindow *mw, const QString &id, const QString &label);
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

