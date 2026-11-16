#pragma once

#include <QDialog>
#include <QString>

#include <functional>

// 通用扩展对话框描述：宿主只认标题、.ui 路径和取数回调，不认识数据源类型。
struct UiDialogSpec
{
    QString title;
    QString uiPath;
    std::function<QString(const QString &widget)> data;
};

// 通用 .ui 对话框引擎：只负责装载 .ui、接按钮和取回调数据。
class UiDialogEngine : public QDialog
{
    Q_OBJECT
public:
    UiDialogEngine(const UiDialogSpec &spec, const QString &path,
                   QWidget *parent = nullptr);

    QString optionsJson() const { return m_options; }

private:
    void collectOptions(QWidget *root);

    UiDialogSpec m_spec;
    QString m_sourcePath;
    QString m_options;
};
