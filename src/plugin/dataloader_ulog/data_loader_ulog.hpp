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
#include "data_loader_base.hpp"

namespace nextpilot {

class QwtSeriesUlog : public QwtSeriesData<QPointF> {
};

class DataLoaderUlog : public DataLoader {
    Q_OBJECT
#if QT_VERSION >= 0x050000
    Q_PLUGIN_METADATA(IID, "nextpilot.plugin.dataloader")
    Q_INTERFACES(DataLoader)
#endif

public:
    DataLoaderUlog();
    ~DataLoaderUlog() override;

    const char *name() const override {
        return "DataLoader ULog";
    }

    QwtSeriesData<QPointF> *getSeriesData(QString name) override;

private:
    std::string _default_time_axis;
    QWidget    *_main_win;
};
} // namespace nextpilot

#endif // __DATA_LOADER_ULOG_H__
