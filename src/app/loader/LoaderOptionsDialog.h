#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QVector>

class QComboBox;
class QFormLayout;
class QJsonObject;
class QLabel;
class QTableWidget;
class QWidget;
struct LoaderPlugin;

// 由 LoaderPlugin::schema 驱动的通用选项对话框。
// 新增一个加载器只需提供 schema + probe，不再在 app 里复制一份 QDialog。
class LoaderOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    LoaderOptionsDialog(const LoaderPlugin &plugin, const QString &path, QWidget *parent = nullptr);

    QString optionsJson() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Field
    {
        QString key;
        QString type;
        QJsonObject spec;
        QWidget *editor = nullptr;
        QLabel *label = nullptr;
    };

    void buildFields();
    void refreshProbe(bool initial);
    void updateVisibility();
    void fillPreview();
    QJsonObject currentOptions() const;
    void setCurrentValue(const QString &key, const QVariant &value);
    QVariant currentValue(const QString &key) const;
    QComboBox *comboFor(const QString &key) const;

    const LoaderPlugin &m_plugin;
    QString m_path;
    QFormLayout *m_form = nullptr;
    QVector<Field> m_fields;
    QHash<QString, QWidget *> m_editors;
    QHash<QString, QWidget *> m_labels;
    QJsonObject m_probe;
    bool m_updating = false;

    QLabel *m_summary = nullptr;
    QTableWidget *m_preview = nullptr;
    QLabel *m_warn = nullptr;
};
