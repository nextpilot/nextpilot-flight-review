#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;
class QTabWidget;
class QSpinBox;

// 协议模板编辑器：设计页（帧头 + 字段表）/ JSON / Lua 三视图，多协议保存。
class ProtocolEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProtocolEditorDialog(const QString &path, QWidget *parent = nullptr);

    QString templatePath() const { return m_path; }

private:
    void refreshDesignFromJson();
    void rebuildJsonFromDesign();
    void rebuildLua();
    void addFieldRow();
    void removeFieldRow();
    void tryFrame();
    void save();
    void saveAs();

    QJsonObject currentTemplate() const;

    QString m_path;
    bool m_syncing = false;

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_name = nullptr;
    QSpinBox *m_frameSize = nullptr;
    QLineEdit *m_sync = nullptr;
    QSpinBox *m_crcOffset = nullptr;
    QLineEdit *m_crcPoly = nullptr;
    QLineEdit *m_crcInit = nullptr;
    QTableWidget *m_fields = nullptr;
    QPlainTextEdit *m_json = nullptr;
    QPlainTextEdit *m_lua = nullptr;
    QLineEdit *m_hex = nullptr;
    QLabel *m_result = nullptr;
};
