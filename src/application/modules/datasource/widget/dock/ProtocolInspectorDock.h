#pragma once

#include "datasource/DecodedFrame.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;

// 通用协议检查面板：只消费 IMessageParser 的 DecodedFrame，不认识具体协议。
class ProtocolInspectorDock : public QWidget
{
public:
    explicit ProtocolInspectorDock(QWidget *parent = nullptr);

    bool loadFile(const QString &path, const QString &protocolId = QString());
    void selectFrame(int index);

private:
    void refreshProtocols();
    void browse();
    void reload();
    void showFrame(int index);
    QString chooseProtocol(const QString &path, const QByteArray &head) const;

    QComboBox *m_protocol = nullptr;
    QLineEdit *m_path = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_frames = nullptr;
    QTableWidget *m_fields = nullptr;
    QVector<PsDecodedFrame> m_frameData;
    QString m_lastPath;
};
