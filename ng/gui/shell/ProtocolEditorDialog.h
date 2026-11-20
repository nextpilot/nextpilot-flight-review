#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantMap>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QWidget;

namespace nextpilot::plotscope::core
{
class AppContext;
class IIntentExecutor;
struct IntentResult;
template <typename T>
class Result;
}

namespace nextpilot::plotscope::gui
{

// 协议编辑器：创建/编辑/校验顺序段协议模板（.psproto），并试算样例帧。
//
// 界面沿用老 ProtocolEditorDialog 的组织方式（QFormLayout 竖排协议属性、
// 段表「字段 / 长度 / 类型 / 取值」、系列协议里变体表与变体段表并排、行可拖拽排序），
// 只把 v1 的键换成 v2 的等价：长度→count、取值→expect、role/checks 文本→段详情里的结构化键。
//
// 「协议」页是日常编辑面；「JSON」页是同一份数据的全文视图，用于高级键与外部交换。
// 权威源始终是 JSON 文本：控件在"落表"时机（切页签/校验/试算/保存）回写一次，
// 不做双向实时同步，避免两处各写一份。
//
// 逻辑全部来自 ng/libs/protocol（validate / parseDocument / previewSequentialFrame），
// 对话框只是这些纯逻辑的 GUI 外壳——不复制任何解码规则。
class ProtocolEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProtocolEditorDialog(QWidget *parent = nullptr,
                                  nextpilot::plotscope::core::AppContext *appCtx = nullptr,
                                  nextpilot::plotscope::core::IIntentExecutor *executor = nullptr);
    ~ProtocolEditorDialog() override;

    static void run(QWidget *parent = nullptr,
                    nextpilot::plotscope::core::AppContext *appCtx = nullptr,
                    nextpilot::plotscope::core::IIntentExecutor *executor = nullptr);

    // 空白模板（供"新建"与测试使用）。
    static QString blankTemplate();

    // 用一段模板文本初始化编辑器（协议页与 JSON 页同时刷新）。
    // 截图工具、以及将来从 Ribbon/命令行带模板打开，都走这一个入口。
    void setTemplateText(const QString &text, const QString &path = QString());

    // 写入样例帧（hex，空格或连写均可），供截图与外部调用。
    void setSampleFrame(const QString &hex);

    // 切到指定页签（0 顶层字段 / 1 帧结构 / 2 子协议 / 3 JSON），供截图与自动化。
    void selectTab(int index);
    void openFirstSegmentDetail();

private:
    void newDoc();
    void openDoc();
    void saveDoc();
    void validateDoc();
    void previewFrame();
    void applyTheme();
    void configureChecksumTransform();

    // --- 协议页 ---
    void buildDesignPages();  // 顶层字段 / 帧结构 / 子协议 三页 + JSON 页
    void refreshFromJson();          // JSON -> 控件（打开/切回协议页时）
    void commitDesign();             // 控件 -> JSON（切页签/校验/试算/保存前）
    QJsonObject collectDoc() const;  // 以 JSON 为底，覆盖表单、段表与变体
    QJsonArray segTableToJson(QTableWidget *table) const;
    void fillDerivedTable(const QJsonArray &derived);
    QJsonArray derivedTableToJson() const;
    QTableWidget *m_derived = nullptr; // 派生页（顶层 derived）
    void fillSegTable(QTableWidget *table, const QJsonArray &segs,
                      const QJsonObject &blocks = QJsonObject());
    void fillVariantsTable(const QJsonArray &variants);
    void reloadVariantSegs();        // 变体表选中行 -> 变体段表
    void storeVariantSegs();         // 变体段表 -> 选中变体的缓存
    void addVariantRow();
    void removeVariantRow();
    void addSegRowAt(QTableWidget *table, int insertAt);
    void removeSegRow(QTableWidget *table, int row);
    void wireSegTable(QTableWidget *table);  // 右键增删 + Delete + 双击段详情
    void openSegmentDetail(QTableWidget *table, int row);
    void markDirty();

    // 单元格字面量：数字原样，字符串带类型信息。
    static QString literalText(const QJsonValue &v);
    static QJsonValue parseLiteral(const QString &text);

    // 段表格三格的界面语法（类型/变换/约束）：人能读、也能反解回 JSON
    QString typeCellText(const QJsonObject &seg) const;
    bool applyTypeCell(const QString &text, QJsonObject &seg, QString *why) const;
    QString transformCellText(const QJsonObject &seg) const;
    QString expectItemText(const QJsonValue &v) const;
    QString expectCellText(const QJsonObject &seg) const;
    bool applyExpectCell(const QString &text, QJsonObject &seg, QString *why) const;

    // 校验/试算一律走 protocol.* 意图（与模块、脚本、CLI 同一份实现）；
    // executor 为空时退回同模块内的 handler 实例，逻辑仍是同一份，不另写规则。
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> runOp(
        const QString &op, const QVariantMap &params);

    nextpilot::plotscope::core::AppContext *m_appCtx = nullptr;
    nextpilot::plotscope::core::IIntentExecutor *m_executor = nullptr;
    QJsonObject m_checksumRequest;  // checksum 变换配方（算子/表达式/Lua）
    QString m_checksumLabel;
    QString m_path;

    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_json = nullptr;
    QLineEdit *m_hex = nullptr;   // 样例帧是单行输入，不需要多行编辑框
    QLabel *m_validation = nullptr;
    QTableWidget *m_preview = nullptr;  // 试算结果：字段 / 偏移 / 值 三列严格对齐
    QLabel *m_status = nullptr;
    QPushButton *m_saveBtn = nullptr;

    // 协议属性（竖排，沿用老编辑器）
    QLineEdit *m_name = nullptr;
    QLineEdit *m_desc = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_layout = nullptr;
    QSpinBox *m_maxsize = nullptr;
    QComboBox *m_endian = nullptr;
    QLineEdit *m_terminator = nullptr;  // text 帧终止标记（可选）
    QLineEdit *m_tsExpr = nullptr;
    QComboBox *m_tsUnit = nullptr;

    // 系列（变体）：变体表与变体段表并排
    QGroupBox *m_seriesBox = nullptr;
    QCheckBox *m_seriesOn = nullptr;
    QWidget *m_seriesBody = nullptr;
    QLineEdit *m_selector = nullptr;
    QLineEdit *m_payload = nullptr;
    QComboBox *m_fallback = nullptr;
    QTableWidget *m_variants = nullptr;
    QTableWidget *m_varSegs = nullptr;

    // 主段表
    QTableWidget *m_segs = nullptr;
    bool m_syncing = false;
    bool m_dirty = false;
    int m_variantRow = -1;   // 变体表选中行
    int m_jsonTab = 0;       // JSON 页索引（切页签时判"是否要落表"）
};

} // namespace nextpilot::plotscope::gui
