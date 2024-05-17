/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include "data_loader_ulog.hpp"

namespace nextpilot {


DataLoaderUlog::DataLoaderUlog() :
    _main_win{nullptr} {
    for (QWidget *widget : qApp->topLevelWidgets()) {
        if (widget->inherits("QMainWindow")) {
            _main_win = widget;
            break;
        }
    }
}

DataLoaderUlog::~DataLoaderUlog() {
}

QwtSeriesData<QPointF> *DataLoaderUlog::getSeriesData(Qstring name) {
}

} // namespace nextpilot
