#pragma once

#include "DataRepository.h"

#include <QMainWindow>

namespace ng::host
{
class ModuleHost;
}

namespace ng::gui
{

class DataManagerPanel;
class DockManager;
class GuiContext;
class PlotCanvas;
class RibbonBar;

// 主窗口：**只做装配**，不含任何业务逻辑。
//
// 这是"modules 是 app 的组成部分、目的是不让 MainWindow 变大"的直接落地：
//   - 窗口骨架（ribbon / dock / 画布）在这里创建与摆放；
//   - 每个功能部件都是独立文件里的独立类（RibbonBar / DockManager /
////    PlotCanvas / DataManagerPanel），主窗口只调用它们的接口；
//   - 业务细节一律不在此文件出现——出问题 grep 一个文件即可定位。
//
// 装配顺序：
//   骨架 -> 声明标准插槽 -> 装器官（器官可自带界面）-> 装工具箱 UI 贡献。
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ng::core::DataRepository &repo, QWidget *parent = nullptr);
    ~MainWindow() override;

    // 启动装配：建骨架、声明插槽、装载模块（GUI 模式）。
    void build(ng::host::ModuleHost &host);

    // 通道被选中（数据面板双击）-> 渲染到中央画布。
    void showChannel(int datasetId, const QString &table, const QString &column);

    // 诊断/测试用。
    RibbonBar *ribbon() const { return m_ribbon; }
    DockManager *docks() const { return m_docks; }
    PlotCanvas *canvas() const { return m_canvas; }
    DataManagerPanel *dataPanel() const { return m_dataPanel; }
    GuiContext *context() const { return m_context; }

private:
    // 声明 app 骨架提供的标准插槽（ribbon 页签/分组、停靠区、菜单）。
    void declareStandardSlots();

    ng::core::DataRepository &m_repo;
    ng::host::ModuleHost *m_host = nullptr; // 用于走意图层取数据
    RibbonBar *m_ribbon = nullptr;
    DockManager *m_docks = nullptr;
    PlotCanvas *m_canvas = nullptr;
    DataManagerPanel *m_dataPanel = nullptr;
    GuiContext *m_context = nullptr;
};

} // namespace ng::gui
