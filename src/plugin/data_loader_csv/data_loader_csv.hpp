/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __DATA_LOADER_CSV_H__
#define __DATA_LOADER_CSV_H__

#include <QObject>
#include <QtPlugin>
#include <QWidget>
#include <QFile>
#include "data_source_base.hpp"

namespace nextpilot {

class DataLoaderCSV : public DataSourceBase {
    Q_OBJECT
#if QT_VERSION >= 0x050000
    Q_PLUGIN_METADATA(IID "nextpilot.plugin.DataSource")
    Q_INTERFACES(nextpilot::DataSourceBase)
#endif

public:
    DataLoaderCSV();
    ~DataLoaderCSV() override;

    const char *name() const override {
        return "DataLoader CSV";
    }

    bool parseColName(QString line);
    bool parseHeader(QFile &file);
    bool readDataFromFile();

    static constexpr int TIME_INDEX_NOT_DEFINED = -2;
    static constexpr int TIME_INDEX_GENERATED   = -1;

private:
    // 分隔符
    std::vector<char> _delimeter;
    // 时间轴
    std::string _timestamp_name  = "_ROW_INDEX_";
    int         _timestamp_index = TIME_INDEX_NOT_DEFINED;
    //
    std::vector<std::string> _column_names;
};

} // namespace nextpilot


#endif // __DATA_LOADER_CSV_H__
