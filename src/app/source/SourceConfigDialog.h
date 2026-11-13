#pragma once

#include "source/SourcePlugin.h"

#include <QDialog>
#include <QHash>
#include <QString>

class QCheckBox;

class QWidget;

// data_source 配置对话框：只按插件 schema 渲染，不出现 MAVLink/串口等专属字段。
class SourceConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SourceConfigDialog(const SourcePlugin &plugin, const QString &initialConfig = QString(),
                                QWidget *parent = nullptr);
    QString configJson() const;

private:
    const SourcePlugin &m_plugin;
    QHash<QString, QWidget *> m_editors;
    QCheckBox *m_record = nullptr;
};
