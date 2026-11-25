#pragma once

#include "IMessageParser.h"

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace nextpilot::plotscope::gui {

// 通用"打开数据"对话框：schema 驱动，不认识任何具体格式。
//
// 架构定位（外壳交互能力）：
//   - 格式知识（有哪些选项、怎么探测）由 IMessageParser::importSchema/probeFile 提供；
//   - 本类只按 schema 渲染控件、调 probe、渲染预览、收集 options——
//     加一种格式只写它的 schema，对话框代码不动。
//   - 这与老架构 DataSourceDialog 同源，但这里依赖 core 的纯数据接口，
//     不碰任何老 C ABI / QWidget 回调；ThemeManager token 上色。
//
// 结果由 result() 给出：path / format / optionsJson（JSON 字符串）。
// MainWindow 拿到后发 data.load 意图（与 CLI/脚本同一条路径）。
class ImportDialog : public QDialog
{
    Q_OBJECT
public:
    struct Result
    {
        QString path;
        QString format;
        QByteArray optionsJson;
        bool ok() const { return !path.isEmpty(); }
    };

    // drivers 是解析目录快照（IMessageParser* 抽象接口列表），由装配侧经
    // AppContext::drivers() 提供——对话框不 include ToolboxHub、不认识工具箱容器。
    explicit ImportDialog(const QList<nextpilot::plotscope::parser::IMessageParser *> &drivers,
                          QWidget *parent = nullptr);
    ~ImportDialog() override;

    Result result() const { return m_result; }

    // 预填文件路径并立即探测（外部带入路径时用，如"打开方式"）。
    void setPath(const QString &path);

    // 静态便捷：弹出对话框，用户确认返回结果；取消返回空 Result。
    static Result run(const QList<nextpilot::plotscope::parser::IMessageParser *> &drivers, QWidget *parent = nullptr);

    // --- 诊断/测试用（#75 验收：不 exec 也能验证 schema 渲染与 probe 链路）---
    int fieldCount() const;          // 已渲染的 schema 字段控件数
    QString currentFormatId() const; // 当前选中的格式驱动 id（空=未选中）
    int previewRowCount() const;     // 探测预览表行数（0=无预览/未探测）
    QString warningText() const;     // probe 警告/错误文案

    // 公开 accept（QDialog 本就是公开 API）：收集 path/format/optionsJson 三元组。
    // 测试不经 exec 直接调用，验证收集链路。
    void accept() override;

private:
    void rebuildFormatList();
    void onFormatChanged();
    void chooseFile();
    void refreshProbe();
    void rebuildFields();
    void updatePreview();

    QList<nextpilot::plotscope::parser::IMessageParser *> m_drivers;
    Result m_result;

    // 控件（前置声明避免头重）
    class Private;
    Private *d;
};

} // namespace nextpilot::plotscope::gui
