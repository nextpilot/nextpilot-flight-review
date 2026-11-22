#pragma once

#include <QFrame>
#include <QWidget>

class QLabel;
class QKeyEvent;
class QMouseEvent;

namespace nextpilot::plotscope::gui
{

// 8×8 悬停选格（抄 main `LayoutPickGrid` 的交互，不是手输对话框）：
//   鼠标划过格子 → 实时高亮前 r×c 块，底部同步显示"3 × 2"；
//   左键落选 → 立即按该规格重排当前页；Esc / 点窗口外部 → 取消。
//
// 之所以不用 QInputDialog：手输要记格式、要数格子、还要处理"×"和"x"两种分隔符，
// 而布局选择本质是"看形状选形状"——直接点比打字快，且能一眼看出 2×3 和 3×2 的差别。
class GridPickGrid : public QWidget
{
    Q_OBJECT
public:
    explicit GridPickGrid(QWidget *parent = nullptr);

    // 当前规格：用暗色外框单独描出，和鼠标 hover 的实色高亮区分开，
    // 这样"点之前我知道现在是什么"。
    void setSpec(int rows, int cols);

signals:
    void picked(int rows, int cols);
    void hovered(int rows, int cols);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRect cellRect(int r, int c) const;

    static constexpr int kMax = 8;   // 与 main 一致：最大 8×8
    static constexpr int kStep = 20; // 每格步距：格 17px + 间隔 3px（实测 main 的 16px 格偏小，8×8 时难点准）
    static constexpr int kPad = 6;   // 四周留白

    int m_hoverRows = 0; // 0 = 无 hover
    int m_hoverCols = 0;
    int m_curRows = 1;
    int m_curCols = 1;
};

// 弹层容器：独立 QFrame(Qt::Popup)。
// 不用 QToolButton::setMenu —— 那只会在按钮上多画一个下拉箭头（main 踩过的坑）。
class GridPickPopup : public QFrame
{
    Q_OBJECT
public:
    explicit GridPickPopup(QWidget *parent = nullptr);

    void setSpec(int rows, int cols);
    bool picked() const { return m_picked; }
    int pickedRows() const { return m_rows; }
    int pickedCols() const { return m_cols; }

signals:
    // 关闭（无论选中还是取消）都发一次，驱动 execGridPick 的局部事件循环退出。
    void finished();

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    void syncHint(int rows, int cols);

    GridPickGrid *m_grid = nullptr;
    QLabel *m_hint = nullptr;
    bool m_picked = false;
    bool m_emitted = false;
    int m_rows = 0;
    int m_cols = 0;
};

// 模态选格：以 globalAnchor（通常是点击时鼠标位置，正好落在「网格」按钮上）为锚点弹层。
// 返回 true 表示用户真的选了规格；false 表示取消（Esc / 点外部）。
// anchor 为空则退化为屏幕居中 —— 脚本调用不至于无处可弹。
bool execGridPick(QWidget *anchor, const QPoint &globalAnchor, int curRows, int curCols,
                  int *outRows, int *outCols);

} // namespace nextpilot::plotscope::gui
