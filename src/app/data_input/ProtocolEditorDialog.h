#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;
class QTabWidget;
class QSpinBox;
class QComboBox;
class QCheckBox;

// 通用帧协议编辑器：固定/变长、同步字偏移、字段常量/范围/CRC 校验、msgid 系列协议。
class ProtocolEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProtocolEditorDialog(const QString &path, QWidget *parent = nullptr);

    QString templatePath() const { return m_path; }

private:
    void setupFieldTable(QTableWidget *t);
    void populateFields(QTableWidget *t, const QJsonArray &fields);
    QJsonArray fieldsFromTable(QTableWidget *t) const;
    void refreshDesignFromJson();
    void rebuildJsonFromDesign();
    void refreshSeqFromJson();
    void rebuildSeqJson();
    void setupSeqTables();
    void populateSeqTable(QTableWidget *t, const QJsonArray &segments);
    QJsonArray seqTableToJson(QTableWidget *t) const;
    void addSeqRow(QTableWidget *t);
    void updateSeqPreview();
    void loadSeqVariant(int row);
    void storeSeqVariantFields();
    void rebuildLua();
    void storeVariantFields();
    void loadVariantRow(int row);
    void addCommonField();
    void addVariantField();
    void tryFrame();
    void save();
    void saveAs();
    QJsonObject currentTemplate() const;

    QString m_path;
    bool m_syncing = false;
    QJsonArray m_variantData;
    int m_currentVariant = -1;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_seqPage = nullptr;
    int m_lastTab = 0;
    QLineEdit *m_seqName = nullptr;
    QLineEdit *m_seqDesc = nullptr;
    QSpinBox *m_seqMaxsize = nullptr;
    QComboBox *m_seqEndian = nullptr;
    QLineEdit *m_seqTimestamp = nullptr;
    QComboBox *m_seqTsUnit = nullptr;
    QJsonObject m_seqTsExtra;
    QTableWidget *m_seqFields = nullptr;
    QCheckBox *m_seqSeries = nullptr;
    QLineEdit *m_seqSelector = nullptr;
    QLineEdit *m_seqPayload = nullptr;
    QComboBox *m_seqFallback = nullptr;
    QTableWidget *m_seqVariants = nullptr;
    QTableWidget *m_seqVarFields = nullptr;
    QLabel *m_seqPreview = nullptr;
    bool m_seqMode = false;
    int m_seqCurrentVariant = -1;
    QJsonArray m_seqVariantData;
    QLineEdit *m_name = nullptr;
    QComboBox *m_frameKind = nullptr;
    QSpinBox *m_frameSize = nullptr;
    QSpinBox *m_lengthByte = nullptr;
    QSpinBox *m_lengthBytes = nullptr;
    QComboBox *m_lengthEndian = nullptr;
    QCheckBox *m_lengthHeader = nullptr;
    QLineEdit *m_sync = nullptr;
    QSpinBox *m_crcOffset = nullptr;
    QLineEdit *m_crcPoly = nullptr;
    QLineEdit *m_crcInit = nullptr;
    QTableWidget *m_fields = nullptr;

    QCheckBox *m_series = nullptr;
    QSpinBox *m_msgIdByte = nullptr;
    QSpinBox *m_msgIdBits = nullptr;
    QTableWidget *m_variants = nullptr;
    QTableWidget *m_varFields = nullptr;

    QPlainTextEdit *m_json = nullptr;
    QPlainTextEdit *m_lua = nullptr;
    QLineEdit *m_hex = nullptr;
    QLabel *m_result = nullptr;
};
