#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QTableWidget;
class QDialogButtonBox;

// CSV 加载选项对话框：先探测、再由用户确认时间轴/分隔符/表头。
// 对应 PlotJuggler 的 data_load_csv 对话框，但不默认"第一列就是时间"。
class CsvLoaderDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CsvLoaderDialog(const QString &path, QWidget *parent = nullptr);

    // 拼给 ps_csv_load_opts 的 JSON
    QString optionsJson() const;

private:
    void reprobe();          // 分隔符变了就重新探测
    void fillColumns();      // 按探测结果填三个列表
    void updateModeUi();     // 时间轴模式联动
    void updateWarnings();

    QString m_path;
    QJsonObject m_probe;
    int m_colCount = 0;

    QComboBox *m_delim = nullptr;
    QCheckBox *m_header = nullptr;
    QSpinBox *m_skip = nullptr;
    QComboBox *m_mode = nullptr;      // 行号 / 指定列 / 日期+时间
    QComboBox *m_timeCol = nullptr;
    QComboBox *m_dateCol = nullptr;
    QComboBox *m_timeOfDay = nullptr;
    QLabel *m_timeLbl = nullptr;
    QLabel *m_dateLbl = nullptr;
    QLabel *m_todLbl = nullptr;
    QTableWidget *m_preview = nullptr;
    QLabel *m_warn = nullptr;
    QLabel *m_summary = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};
