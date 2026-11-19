#pragma once

#include <QWidget>

#include <QVector>

class QGridLayout;
class QStackedWidget;
class QEvent;

namespace nextpilot::plotscope::core
{
class AppContext;
class IIntentExecutor;
}

namespace nextpilot::plotscope::gui
{

class ChannelView;
class InstrumentView;
class PlotCanvas;

// 画布工作区：中央区域的组合容器，管理 N 个画布 cell（网格布局）。
//
// 为什么要这一层（而不是让外壳直接摆一个 PlotCanvas）：
//   - 老代码 page.new / 拆分 / 网格排布是"每页 N 个画布"，一个画布承载不了；
//   - view action（plot.*）若由每个画布各自注册，后建的会劫持先建的（QHash 覆盖）——
//     必须由**唯一仲裁者**集中注册一次，路由到当前活动 cell；
//   - 活动通道（数据集树双击/拖拽）只追加到活动 cell，符合"当前画布"语义。
//
// 分工：本类只做布局与路由（QWidget 组合），取数仍走 ChannelView→意图层，
// 绘制仍是 PlotCanvas→fastplot。多画布不复制数据、不新增数据概念。
class CanvasWorkspace : public QWidget
{
    Q_OBJECT
public:
    CanvasWorkspace(core::AppContext &appCtx, core::IIntentExecutor &executor,
                    QWidget *parent = nullptr);
    ~CanvasWorkspace() override;

    int cellCount() const;
    int activeIndex() const;
    // 当前活动画布（无画布时返回 nullptr）。仅供视图内部路由，不交给外壳。
    PlotCanvas *activeCanvas() const;
    ChannelView *activeChannelView() const;

    // 网格规格（行×列），截图/状态显示用。
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

private:
    struct Cell
    {
        PlotCanvas *canvas = nullptr;
        ChannelView *view = nullptr;
        InstrumentView *instrument = nullptr;
        QStackedWidget *stack = nullptr;
    };

    // —— cell 操作（view action 处理器调用）——
    int addCell();                       // 在末尾追加一个画布并激活
    bool removeAt(int index);            // 删除 cell（至少保留一个）
    bool split(bool horizontal);         // 拆分活动 cell：水平=左右，垂直=上下
    bool setGrid(int rows, int cols);    // 重排为 rows×cols 网格
    void setActive(int index);           // 设为活动 cell（点击/程序指定）
    void relayout();                     // 按 m_rows/m_cols 重排所有 cell 到网格

    // 在指定 cell 内切图型：曲线类画布内切；专用图型把 cell 的 view 换成仪表。
    bool setCellType(int index, const QString &kind);

    // 集中注册 view action（唯一注册点，路由到 active cell）。
    void registerViewActions();
    // 注册曲线/轴/画布的属性 provider 与 setter（属性面板按 kind 路由到这里）。
    void registerPropertyProviders();
    // 连接一个画布的选中请求信号 -> AppContext.selection。
    void connectCanvasSignals(PlotCanvas *canvas);

    void onActiveChannelChanged(int datasetId, const QString &table, const QString &column);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    core::AppContext &m_appCtx;
    core::IIntentExecutor &m_executor;
    QGridLayout *m_grid = nullptr;
    QVector<Cell> m_cells;
    int m_active = 0;
    int m_rows = 1;
    int m_cols = 1;

    friend QWidget *createCanvasWorkspace(core::AppContext &appCtx,
                                          core::IIntentExecutor &executor, QWidget *parent);
};

// 工厂：注册进部件表，供 PlotModule::onAttach 申请（"canvas-workspace"）。
QWidget *createCanvasWorkspace(core::AppContext &appCtx, core::IIntentExecutor &executor,
                               QWidget *parent = nullptr);

} // namespace nextpilot::plotscope::gui
