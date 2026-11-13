#pragma once

#include <QDialog>
#include <QString>

struct DataInputPlugin;

// 通用插件对话框：加载插件目录里的 .ui，不包含任何格式专属控件。
class PluginDialogEngine : public QDialog
{
    Q_OBJECT
public:
    PluginDialogEngine(const DataInputPlugin &plugin, const QString &path, QWidget *parent = nullptr);

    QString optionsJson() const { return m_options; }

private:
    QString m_options;
};
