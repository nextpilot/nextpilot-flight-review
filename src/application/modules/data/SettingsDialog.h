#pragma once

#include "IIntentExecutor.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;

// 数据设置对话框。
//
// 每一组都必须有**真实消费方**（点了不生效的键一律不上，避免假入口）：
//   · export.csv.maxPoints —— data.export 的采样上限（0 = 不限）
//   · derive.maxPoints     —— data.derive 的重算上限（0 = 不限）
//   · 解析缓存             —— data.cache.stats 展示 + data.clearCache 一键清
//   · plot.time.format     —— 画布 X 轴刻度三态（relative|hms|utc）：
//       建格时 CanvasWorkspace::makeCell 读该键，保存后 DataManagerPanel 派发
//       viewAction plot.timeFormat，遍历所有页所有格立即换刻度文本。
//       实现是 fastplot::Axis 的 tick-label formatter（只改文本不改刻度位置）。
//
// 画布组（默认网格/默认图例）仍未做：那两个键没有统一的持久消费方，
// 摆上去就是假入口——等有真实读取点再上。
//
// 读写都走意图层（settings.get/set、data.cache.stats/clearCache），
// 与脚本/CLI 同一条出口，GUI 不直接碰 SettingsStore。
namespace nextpilot::plotscope::gui
{

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(nextpilot::plotscope::intent::IIntentExecutor &executor,
                            QWidget *parent = nullptr);

private:
    void buildUi();
    void refreshCacheLabel();
    void save();

    nextpilot::plotscope::intent::IIntentExecutor &m_executor;

    QSpinBox *m_exportMax = nullptr;
    QSpinBox *m_deriveMax = nullptr;
    QComboBox *m_timeFormat = nullptr; // 时间轴刻度：相对秒 / 时分秒 / UTC
    QLabel *m_cacheLabel = nullptr;
    QPushButton *m_clearCache = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace nextpilot::plotscope::gui
