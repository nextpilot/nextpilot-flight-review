#pragma once

#include "IIntentExecutor.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::gui
{

// 全局偏好设置对话框（「文件 → 设置」/ Ctrl+ / app.call("settings.dialog")）。
//
// 作用域：只装 **App 作用域**——跨工作区、跨会话持久的用户偏好。
// Workspace 的窗口几何 / dock 排列 / 工作区配方属于「会话状态」，
// 不进这里（那是 SaveState/RestoreState 的事）。
//
// 每组都必须有**真实消费方**（与数据设置同一纪律，不上假开关）：
//   app/theme            → ThemeManager::apply（全局 QSS / ribbon / dock 样式）
//   plot.time.format     → PlotWorkspace::makeCell 的 X 轴刻度文本
//   export.csv.maxPoints → data.export 采样上限（0 = 不限）
//   derive.maxPoints     → data.derive 重算上限（0 = 不限）
//   解析缓存              → data.cache.stats 展示 + data.clearCache 一键清
//
// 读写全部走意图层（settings.get/set/remove/flush、theme.set、data.cache.*），
// 与 CLI/脚本同一条出口；保存末尾 flush 落盘到 AppData 下的 settings.json。
//
// 放 shell/ 而非 views/：偏好是**系统级**外壳能力，不属于任何功能模块，
// 与 ProtocolEditorDialog / ImportDialog 同层。
class PreferencesDialog final : public QDialog
{
    Q_OBJECT
public:
    // initialSection：首屏分组 "appearance" / "timeline" / "data"，空=外观。
    PreferencesDialog(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                      const QString &initialSection = QString(), QWidget *parent = nullptr);

private:
    void buildUi();
    QWidget *buildAppearancePage();
    QWidget *buildTimelinePage();
    QWidget *buildDataPage();
    void loadValues();
    void save();
    void resetToDefaults();
    void refreshCacheLabel();
    void applyTimeline();
    void selectSection(const QString &initialSection);

    context::AppContext &m_appCtx;
    intent::IIntentExecutor &m_executor;

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_timeFormat = nullptr;
    QComboBox *m_startupRecovery = nullptr; // 启动恢复档位 off/layout/full
    QSpinBox *m_exportMax = nullptr;
    QSpinBox *m_deriveMax = nullptr;
    QLabel *m_cacheLabel = nullptr;
    QPushButton *m_clearCache = nullptr;
    QLabel *m_pathLabel = nullptr; // 设置文件实际路径（或"内存模式"）
    QLabel *m_status = nullptr;    // 保存结果 / 操作提示
};

} // namespace nextpilot::plotscope::gui
