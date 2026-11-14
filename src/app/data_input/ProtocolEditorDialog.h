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
