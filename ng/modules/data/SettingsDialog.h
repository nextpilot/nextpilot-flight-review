#pragma once

#include "IIntentExecutor.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QSpinBox;

// 数据设置对话框（M4/T4.5，数据组）。
//
// 为什么只放"数据组"：画布/时间轴两组的键目前没有任何消费方，先摆上去就是
// 假入口（点了不生效）。这里只放**已经真实消费**的三个设置：
//   · export.csv.maxPoints —— data.export 的采样上限（0 = 不限）
//   · derive.maxPoints     —— data.derive 的重算上限（0 = 不限）
//   · 解析缓存             —— data.cache.stats 展示 + data.clearCache 一键清
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
    QLabel *m_cacheLabel = nullptr;
    QPushButton *m_clearCache = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace nextpilot::plotscope::gui
