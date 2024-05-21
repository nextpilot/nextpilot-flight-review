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
#include <QMessageBox>
#include <QSyntaxStyle>
#include <QProgressDialog>
#include <QRegularExpression>
// #include <stdexcept>
#include "data_loader_csv.hpp"

namespace nextpilot {

DataLoaderCSV::DataLoaderCSV() {
}

DataLoaderCSV::~DataLoaderCSV() {
}

bool DataLoaderCSV::parseColName(QString line) {
    QString     delimeter = "\\s+|,";
    QStringList names     = line.split(QRegularExpression(delimeter));


    int is_number_count = 0;
    for (int col = 0; col < names.size(); col++) {
        bool is_number;
        names[col].trimmed().toDouble(&is_number);
        if (is_number) {
            is_number_count++;
        }
    }

    // The first line should contain the header. If it contains a number, we will
    // apply a name ourselves

    if (is_number_count == names.size()) {
        for (int col = 0; col < names.size(); col++) {
            QString name = QString("_col_%1").arg(col);
            _column_names.push_back(name.toStdString());
        }
    } else {
        for (int col = 0; col < names.size(); col++) {
            QString name = names[col].trimmed();
            if (name.isEmpty()) {
                name = QString("_col_%1").arg(col);
            }
            _column_names.push_back(name.toStdString());
        }
    }

    // 判断是否有重复的标题
    for (int i = 0; i < _column_names.size(); i++) {
        auto name  = _column_names[i];
        int  index = 0;
        for (int j = i + 1; j < _column_names.size(); j++) {
            if (name == _column_names[j]) {
                _column_names[j] += QString("_%1").arg(++index).toStdString();
            }
        }
        if (index > 0) {
            _column_names[i] += "_0";
        }
    }
    return true;
}

bool DataLoaderCSV::parseHeader(QFile &file) {
    _column_names.clear();

    file.open(QFile::ReadOnly);
    QTextStream in(&file);

    QString     preview_lines;
    QStringList lines;

    bool header_found = false;
    for (int row = 0; row <= 100 && !in.atEnd(); row++) {
        auto line      = in.readLine();
        preview_lines += line + "\n";
        lines.push_back(line);
        // 第一个非空白行
        if (!header_found && !line.trimmed().isEmpty()) {
            header_found = parseColName(line);
        }
    }
    file.close();

    return true;
}

bool DataLoaderCSV::readDataFromFile() {
    QString filename = "";
    QFile   file(filename);


    // count the number of lines first
    int total_lines = 0;
    {
        file.open(QFile::ReadOnly);
        QTextStream in(&file);
        while (!in.atEnd()) {
            in.readLine();
            total_lines++;
        }
        file.close();
    }

    QProgressDialog dlg;
    dlg.setWindowTitle("Loading the CSV file");
    dlg.setLabelText("Loading... please waiting");
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setRange(0, total_lines - 1);
    dlg.setAutoClose(true);
    dlg.setAutoReset(true);
    dlg.show();

    {
        file.open(QFile::ReadOnly);
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            // 跳过空行
            if (line.trimmed().isEmpty()) {
                continue;
            }
            // 拆分字符串
            QStringList items = line.split(",");
            //
            if (items.size() != _column_names.size()) {
                QMessageBox msgbox;
                msgbox.setWindowTitle("Error Reading File");
                msgbox.addButton(QMessageBox::Ok);
                msgbox.exec();
                return false;
            }
        }
        file.close();
    }

    return true;
}

} // namespace nextpilot
