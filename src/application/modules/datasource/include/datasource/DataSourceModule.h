#pragma once

#include "Component.h"
#include <QPair>
#include <QString>
#include <QVector>

class MainWindow;
struct PsChannelInfo;
namespace ps::data_source { struct SourceData; struct SourceStore; }
namespace ps::components {
ps::data_source::SourceStore *dataSourceStore(MainWindow *mw);
const QVector<const PsChannelInfo *> &dataSourceChannels(MainWindow *mw);
QVector<QPair<double, QString>> &dataSourceTimeMarkers(MainWindow *mw);
void addDataSourceTimeMarker(MainWindow *mw, double t, const QString &label, bool prompt);
void removeDataSourceTimeMarker(MainWindow *mw, double t);
void moveDataSourceTimeMarker(MainWindow *mw, double from, double to);
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

