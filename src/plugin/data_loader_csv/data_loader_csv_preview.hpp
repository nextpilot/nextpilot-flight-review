#ifndef DATA_LOADER_CSV_PREVIEW_HPP
#define DATA_LOADER_CSV_PREVIEW_HPP

#include <QDialog>

namespace Ui {
class data_loader_csv_preview;
}

class data_loader_csv_preview : public QDialog
{
    Q_OBJECT

public:
    explicit data_loader_csv_preview(QWidget *parent = nullptr);
    ~data_loader_csv_preview();

private slots:
    void on_pushButton_browser_clicked();

private:
    Ui::data_loader_csv_preview *ui;
};

#endif // DATA_LOADER_CSV_PREVIEW_HPP
