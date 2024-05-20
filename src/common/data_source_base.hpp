/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __DATA_SOURCE_BASE_H__
#define __DATA_SOURCE_BASE_H__


#pragma once

#include <QtPlugin>
#include <QMenu>
#include <QWidget>
#include <QDomDocument>
#include "qwt_series_data.h"

namespace nextpilot {


class DataSourceBase : public QObject {
public:
    DataSourceBase() = default;

    virtual ~DataSourceBase() = default;

    virtual const char *name() const {
        return nullptr;
    }

    virtual QwtSeriesData<QPointF> *getSeriesData(QString name) {
        return nullptr;
    }
};

using DataSourceBasePtr = std::shared_ptr<DataSourceBase>;

class DataSourceBaseFile : public DataSourceBase {
};

} // namespace nextpilot

QT_BEGIN_NAMESPACE
Q_DECLARE_INTERFACE(nextpilot::DataSourceBase, "nextpilot.plugin.DataSource")
QT_END_NAMESPACE

#endif // __DATA_SOURCE_BASE_H__
