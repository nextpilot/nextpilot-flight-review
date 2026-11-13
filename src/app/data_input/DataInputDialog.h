#pragma once

#include "data_input/DataInputPlugin.h"

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QListWidget;
class QTableWidget;
class QVBoxLayout;
class QWidget;

// 数据输入统一配置框：DataInput = Transport + Parser(s) + Options。
//   Transport 由 APP 按 supported/defaultURI 渲染，插件只声明支持哪些、默认值；
//   Parser 由插件 schema 提供字段，可多选，每个 parser 展示自己的配置。
class DataInputDialog : public QDialog
{
    Q_OBJECT
public:
    DataInputDialog(const DataInputPlugin &plugin, const QString &path, QWidget *parent = nullptr);
    DataInputDialog(const QVector<DataInputPlugin> &parsers, const QString &preferredId,
                    const QString &initialConfig = QString(), QWidget *parent = nullptr);

    // 兼容旧 DataInputPlugin::open 的单 parser 扁平配置。
    QString optionsJson() const;
    QString configJson() const { return optionsJson(); }
    // 新的 DataInput 模型：transport 对象 + parsers 数组。
    QString dataInputJson() const;

    QString primaryParserId() const;
    QStringList selectedParserIds() const;
    int selectedParserCount() const;

private:
    struct Field
    {
        QString key;
        QString type;
        QJsonObject spec;
        QWidget *editor = nullptr;
        QLabel *label = nullptr;
    };

    struct ParserPane
    {
        QString id;
        QString label;
        QString kind;
        QStringList transports;
        QJsonObject schema;
        QJsonObject defaults;
        QJsonObject initial;
        bool recordable = false;
        bool replayable = false;
        bool probeAvailable = false;
        QString fileFilter;
        std::function<QJsonObject(const QString &, const QJsonObject &)> probeFn;
        QGroupBox *box = nullptr;
        QFormLayout *form = nullptr;
        QHash<QString, QWidget *> editors;
        QVector<Field> fields;
        QJsonObject probeData;
    };

    void initUi();
    void buildTransportFields();
    void buildParserPanes();
    void rebuildParserSelection();
    void updateParserVisibility();
    void updateTransportAvailability();
    void applyInitialFromProbe(ParserPane &pane);
    void populateChoices(ParserPane &pane);
    void refreshProbe(bool initial);
    void updateVisibility(ParserPane &pane);
    void fillPreview();
    QJsonObject parserOptions(const ParserPane &pane) const;
    QJsonObject transportOptions() const;
    QJsonObject currentPrimaryOptions() const;
    void collectTransportValues();
    void setCurrentValue(ParserPane &pane, const QString &key, const QVariant &value);
    QVariant currentValue(const ParserPane &pane, const QString &key) const;
    QComboBox *comboFor(const ParserPane &pane, const QString &key) const;
    QWidget *createEditor(const QJsonObject &f, const QJsonValue &value,
                          QHash<QString, QWidget *> &editors, QFormLayout *form);
    void applyTransportDefaultUri(const QString &uri);
    QString currentTransportType() const;
    bool parserSupportsTransport(const ParserPane &pane, const QString &transport) const;
    ParserPane *primaryPane();
    const ParserPane *primaryPane() const;

    bool m_sourceMode = false;
    QString m_path;
    QString m_title;
    QString m_subtitle;
    QStringList m_transports;
    QString m_defaultTransport;
    QString m_defaultUri;
    QJsonObject m_transportDefaults;
    QJsonObject m_initial;
    QJsonObject m_transportValues;
    QString m_fileFilter;
    QString m_preferredId;
    bool m_fixedPath = false;
    bool m_rawCapture = false;
    bool m_recordable = false;
    bool m_probeAvailable = false;
    bool m_updating = false;

    QVector<ParserPane> m_panes;
    QComboBox *m_transport = nullptr;
    QWidget *m_transportRow = nullptr;
    QFormLayout *m_transportForm = nullptr;
    QHash<QString, QWidget *> m_transportEditors;

    QGroupBox *m_parserBox = nullptr;
    QLabel *m_parserTitle = nullptr;
    QListWidget *m_parserList = nullptr;
    QVBoxLayout *m_parserLayout = nullptr;

    QCheckBox *m_record = nullptr;
    QLabel *m_summary = nullptr;
    QTableWidget *m_preview = nullptr;
    QLabel *m_warn = nullptr;
};
