#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

namespace ps::filter
{
struct ParamDef;
}

class QFormLayout;

// 由共享算子目录生成参数控件；datasource 与 plotting 共用同一套表单。
class FilterParamEditor : public QWidget
{
    Q_OBJECT

public:
    explicit FilterParamEditor(QWidget *parent = nullptr);

    void setFilter(const QString &id, const QJsonObject &initial = QJsonObject());
    QString filterId() const { return m_id; }
    QJsonObject params() const;

signals:
    void changed();

private:
    struct Field
    {
        const ps::filter::ParamDef *def = nullptr;
        QWidget *widget = nullptr;
    };

    void clearForm();

    QFormLayout *m_form = nullptr;
    QString m_id;
    QVector<Field> m_fields;
};
