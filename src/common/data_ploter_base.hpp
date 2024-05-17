/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#ifndef __DATA_PLOTER_BASE_H__
#define __DATA_PLOTER_BASE_H__

template <typename TimeType, typename ValueType>
class TimeSeriesBase {
    virtual ValueType &getValueAt(TimeType) {
    }
};

class PlotDataMapRef {
};


#endif // __DATA_PLOTER_BASE_H__
