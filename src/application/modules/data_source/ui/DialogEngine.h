#pragma once

#include <QDialog>
#include <QString>

struct DataSourceEntry;

// 通用扩展对话框：加载扩展目录里的 .ui，不包含任何格式专属控件。
class ExtensionDialogEngine : public QDialog
{
    Q_OBJECT
public:
    ExtensionDialogEngine(const DataSourceEntry &entry, const QString &path, QWidget *parent = nullptr);

    QString optionsJson() const { return m_options; }

private:
    QString m_options;
};
