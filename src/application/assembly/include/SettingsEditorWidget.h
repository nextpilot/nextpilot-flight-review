#pragma once

// 内置 Settings editor 的内容控件。
// 设置 = 一种编辑器（viewType workbench.editor.settings2，owner core.settings），
// 经 M3-a 的 registerEditorProvider 注册，壳不特判；本控件只依赖 workbench 的
// IConfigurationService（声明目录 + 分层读写 + reset），不碰 registry、不读配置文件。
//
// UI 规格：
//   - 顶部：搜索（key + description 子串）、「Modified」过滤、JSON 视图切换；
//   - 左目录：各 configuration 按 (order, title)，零 owner 也有 core.settings 的「通用」组；
//   - 右侧：按属性元数据生成控件（bool→复选、enum→下拉、number→带 min/max、
//     string→输入、editPresentation=multiline→多行）；逐项「重置」；层级提示（来源层）；
//     已修改项左侧「●」标记（对位 VSCode 的 modified 蓝条）。
//
// 测试驱动面（GUI e2e 用公开 API，不做合成鼠标事件）：setSearch / setModifiedFilter /
// setJsonView / jsonText / visibleSettingCount / setSettingValue / layerHintOf。

#include "IConfigurationService.h"
#include "ResourceRef.h"

#include <QJsonValue>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;

namespace nextpilot::plotscope::assembly {

class SettingsEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsEditorWidget(nextpilot::plotscope::workbench::IConfigurationService &svc,
                                  QWidget *parent = nullptr);
    void setResourceRef(const nextpilot::plotscope::registry::ResourceRef &ref) { m_ref = ref; }

    // ---- 测试驱动面 ----
    void setSearch(const QString &text);
    void setModifiedFilter(bool on);
    void setJsonView(bool on);       // true = JSON 视图（只读文本），false = 属性表
    QString jsonText() const;        // 当前 JSON 视图内容（全部已声明键的 effective 值）
    int visibleSettingCount() const; // 当前属性表可见行数（过滤后）
    // 控件写入走服务（对位用户改控件）：写 user 层。
    void setSettingValue(const QString &key, const QJsonValue &value);
    // 显式刷新（服务在控件外被改后重建行与 JSON；行内 reset 按钮同样走它）。
    void refresh();
    // 按 ConfigurationSpec.id 精确聚焦（对位 VSCode openSettings(query) 的 @category 定位）：
    // 命中 spec.id → 只显示该组属性（记录其 category 过滤），清搜索框避免叠加过滤；
    // 无命中 → 退回全屏并出诊断，不静默吞参数。返回值 = 是否命中（供调用方/测试断言）。
    bool focusSection(const QString &section);
    // 最近一次聚焦的状态（空 = 未聚焦；否则为诊断串，命中和未命中都有可读反馈）。
    QString focusDiagnostic() const { return m_focusDiagnostic; }
    // 本页签对应的资源（§3.0.12：中央区页签必须能回答"打开的是哪个 ResourceRef"）。
    nextpilot::plotscope::registry::ResourceRef resourceRef() const { return m_ref; }
    // 层级提示：默认 / user / workspace / document。
    QString layerHintOf(const QString &key) const;

private:
    void rebuild();     // 按目录选择 + 搜索 + Modified 过滤重建属性表
    void rebuildJson(); // 重建 JSON 视图文本
    bool matchesFilter(const nextpilot::plotscope::registry::ConfigurationProperty &prop) const;
    bool isModified(const QString &key) const;
    QString firstSetLayer(const QString &key) const;

    nextpilot::plotscope::workbench::IConfigurationService &m_svc;

    QLineEdit *m_search = nullptr;
    QCheckBox *m_modifiedOnly = nullptr;
    QListWidget *m_toc = nullptr; // 目录（含「全部」）
    QScrollArea *m_scroll = nullptr;
    QPlainTextEdit *m_jsonView = nullptr;
    QLabel *m_layerBanner = nullptr; // 顶部层级提示（写哪层）
    struct Row
    {
        QString key;
        QWidget *widget = nullptr; // 行容器（可见性即过滤）
        QLabel *sourceLabel = nullptr;
        QLabel *keyLabel = nullptr;
        bool modifiedMarked = false;
    };
    QList<Row> m_rows;
    bool m_jsonMode = false;
    nextpilot::plotscope::registry::ResourceRef m_ref; // createEditor 传入的资源
    QString m_focusCategory;                           // 非空 = 按 spec.id 聚焦命中后的 category（只显示该组）
    QString m_focusDiagnostic;                         // 最近一次 focusSection 的反馈
};

} // namespace nextpilot::plotscope::assembly
