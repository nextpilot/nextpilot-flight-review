#include "data_loader_csv_preview.hpp"
#include "ui_data_loader_csv_preview.h"

data_loader_csv_preview::data_loader_csv_preview(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::data_loader_csv_preview)
{
    ui->setupUi(this);
}

data_loader_csv_preview::~data_loader_csv_preview()
{
    delete ui;
}

void data_loader_csv_preview::on_pushButton_browser_clicked()
{

}

