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
#include "data_loader_csv.hpp"

namespace nextpilot {

DataLoaderCSV::DataLoaderCSV() {
}

DataLoaderCSV::~DataLoaderCSV() {
}

bool DataLoadCSV::parseColName(QString line) {
    QStringList names = line.split(QRegularExpression(_delimeter));


    int is_number_count = 0;
    for (int col = 0; col < names.size(); col++) {
        bool is_number;
        names[i].trimmed.toDouble(&is_number);
        if (is_number) {
            is_number_count++;
        }
    }

    if (is_number_count == names.size()) {
        for (int col = 0; col < names.size(); col++) {
            QString name = QString("_col_%1").arg(i);
            _column_names.push_back(name.toStdString());
        }
    } else {
        for (int col = 0; col < names.size(); col++) {
            QString name = names[i].trimmed();
            if (name.isEmpty()) {
                name = QString("_col_%1").arg(i);
            }
            _column_names.push_back(name.toStdString());
        }
    }
}

bool DataLoaderCSV::parseHeader(QFile &file) {
    _column_names.clear();

    QTextStream stream(&file);

    QString     preview_lines;
    QStringList lines;

    bool table_header_found = false;
    for (int row = 0; row <= 100 && !stream.atEnd(); row++) {
        auto line      = stream.readLine();
        preview_lines += line + "\n";
        lines.push_back(line);
        // 第一个非空白行
        if (!table_header_found && !line.trimmed().isEmpty()) {
            table_header_found = true;
            QStringList names  = line.split(QRegularExpression(_delimeter));
            for (int col = 0; col < names.size(); col++) {
                bool is_number;
                names[i].trimmed.toDouble(&is_number);
                if (is_number) {
                }
            }
        }
    }

    // The first line should contain the header. If it contains a number, we will
    // apply a name ourselves
    QString first_line = stream.readLine();
}

bool DataLoaderCSV::readDataFromFile() {
    QString filename = "";
    QFile   file(filename);

    file.open(QFile::ReadOnly);
}

} // namespace nextpilot
