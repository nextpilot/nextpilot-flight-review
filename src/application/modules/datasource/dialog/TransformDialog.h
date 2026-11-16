#pragma once

#include <QDialog>

#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

struct PsDataset;
struct PsChannelInfo;

struct TransformDialogResult
{
    QStringList addedNames;
    QString inputName;
    QString kind;
    QString filterId;
    QJsonObject params;
    QString outputName;
    QString quat[4];
    bool degrees = true;
    bool unwrap = true;
};

class QComboBox;
class QLineEdit;
class QStackedWidget;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QTimer;
class PreviewPlot;

// 数据源侧派生通道：单通道滤波器 / 四元数 4→3。
// 应用时调用内核写回 PsDataset，DataSourceSession 记录 recipe 并刷新数据树。
class TransformDialog : public QDialog
{
    Q_OBJECT

public:
    TransformDialog(PsDataset *ds, const QVector<const PsChannelInfo *> &channels,
                    const QString &sourceName, const QString &preselect = QString(),
                    QWidget *parent = nullptr);

    QStringList addedNames() const { return m_added; }
    QString inputName() const { return m_inputName; }
    QString kind() const { return m_kind; }
    QString filterId() const { return m_filterId; }
    QJsonObject params() const { return m_params; }
    QString outputName() const { return m_outputName; }
    QString w() const { return m_quat[0]; }
    QString x() const { return m_quat[1]; }
    QString y() const { return m_quat[2]; }
    QString z() const { return m_quat[3]; }
    bool degrees() const { return m_degreesOn; }
    bool unwrap() const { return m_unwrapOn; }
    TransformDialogResult transformResult() const;

private:
    QWidget *buildFilterPage();
    QWidget *buildQuatPage();
    QWidget *buildParamsPage(const QString &id);
    void onFilterChanged(int index);
    void apply();
    void refreshOutputName();
    QJsonObject currentParams(const QString &id) const;
    void updatePreview();

    PsDataset *m_ds = nullptr;
    QVector<const PsChannelInfo *> m_channels;
    QStringList m_added;
    QString m_inputName;
    QString m_preselect;
    QString m_kind;
    QString m_filterId;
    QString m_outputName;
    QJsonObject m_params;
    QString m_quat[4];
    bool m_degreesOn = true;
    bool m_unwrapOn = true;

    QComboBox *m_filter = nullptr;
    QComboBox *m_input = nullptr;
    QLineEdit *m_output = nullptr;
    QStackedWidget *m_paramStack = nullptr;
    bool m_autoOutput = true;
    PreviewPlot *m_preview = nullptr;
    QTimer *m_previewTimer = nullptr;

    // 单通道参数控件，键为 <filter>.<param>
    QHash<QString, QDoubleSpinBox *> m_doubles;
    QHash<QString, QSpinBox *> m_ints;
    QHash<QString, QCheckBox *> m_checks;
    QHash<QString, QComboBox *> m_combos;

    QComboBox *m_qw = nullptr;
    QComboBox *m_qx = nullptr;
    QComboBox *m_qy = nullptr;
    QComboBox *m_qz = nullptr;
    QLineEdit *m_qout = nullptr;
    QCheckBox *m_degrees = nullptr;
    QCheckBox *m_unwrap = nullptr;
};
