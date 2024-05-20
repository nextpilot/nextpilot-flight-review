/*****************************************************************
 *     _   __             __   ____   _  __        __
 *    / | / /___   _  __ / /_ / __ \ (_)/ /____   / /_
 *   /  |/ // _ \ | |/_// __// /_/ // // // __ \ / __/
 *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_
 * /_/ |_/ \___//_/|_| \__//_/    /_//_/ \____/ \__/
 *
 * Copyright All Reserved © 2015-2024 NextPilot Development Team
 ******************************************************************/

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QDebug>
#include <QSettings>
#include <QMainWindow>
#include <stdexcept>
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

bool DataLoaderUlog::readDataFromFile() {
    const QString filename = "";
    QFile         file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("ULog: Failed to open file");
    }

    QByteArray qbarray = file.readAll();
    _reader.readChunk((const uint8_t *)qbarray.data(), qbarray.size());

    // Check for errors
    if (_container->hadFatalError()) {
        qDebug() << "Fatal parsing error, exiting";
        return false;
    }

    if (!_container->parsingErrors().empty()) {
        qDebug() << "###### File Parsing Errors ######";
        for (const auto &parsing_error : _container->parsingErrors()) {
            qDebug() << parsing_error.c_str();
        }
    }


    // List all subscription names
    auto subscription_names = _container->subscriptionNames();
    for (const auto &sub : subscription_names) {
        qDebug() << sub.c_str();
    }

    return true;
}

QwtSeriesData<QPointF> *DataLoaderUlog::getSeriesData(QString name) {
    auto subscription_names = _container->subscriptionNames();

    if (subscription_names.find(name.toStdString()) != subscription_names.end()) {
        const auto subscription = _container->subscription(name.toStdString());
    }

    return nullptr;
}

} // namespace nextpilot
