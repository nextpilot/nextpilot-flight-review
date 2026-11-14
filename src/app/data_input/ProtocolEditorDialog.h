#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;

// 协议模板编辑器：JSON 模板 + 字段位域表 + 样例帧 hex + CRC/字段试算。
class ProtocolEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProtocolEditorDialog(const QString &path, QWidget *parent = nullptr);

    QString templatePath() const { return m_path; }

private:
    void reloadTable();
    void tryFrame();
    void save();

    QString m_path;
    QPlainTextEdit *m_json = nullptr;
    QTableWidget *m_fields = nullptr;
    QLineEdit *m_hex = nullptr;
    QLabel *m_result = nullptr;
};
