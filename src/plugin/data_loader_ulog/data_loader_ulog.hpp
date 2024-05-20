/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __DATA_LOADER_ULOG_H__
#define __DATA_LOADER_ULOG_H__

#include <QObject>
#include <QtPlugin>
#include <QWidget>
#include "data_source_base.hpp"
#include "ulog_cpp/data_container.hpp"
#include "ulog_cpp/reader.hpp"

namespace nextpilot {

class QwtSeriesUlog : public QwtSeriesData<QPointF> {
};

class DataLoaderUlog : public DataSourceBase {
    Q_OBJECT
#if QT_VERSION >= 0x050000
    Q_PLUGIN_METADATA(IID "nextpilot.plugin.DataSource")
    Q_INTERFACES(nextpilot::DataSourceBase)
#endif

public:
    DataLoaderUlog();
    ~DataLoaderUlog() override;

    const char *name() const override {
        return "DataLoader ULog";
    }

    bool readDataFromFile();

    QwtSeriesData<QPointF> *getSeriesData(QString name) override;

private:
    std::shared_ptr<ulog_cpp::DataContainer> _container{std::make_shared<ulog_cpp::DataContainer>(ulog_cpp::DataContainer::StorageConfig::FullLog)};
    ulog_cpp::Reader _reader{_container};

    std::string _default_time_axis;
    QWidget    *_main_win;
};
} // namespace nextpilot

#endif // __DATA_LOADER_ULOG_H__
