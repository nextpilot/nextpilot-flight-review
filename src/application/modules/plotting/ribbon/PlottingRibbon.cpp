#include "PlottingRibbon.h"
#include "MainWindow.h"
#include "RibbonGallery.h"
#include "ThemeManager.h"
#include "plotting/PlottingModule.h"
#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QToolButton>
#include <QActionGroup>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include "SARibbonBar.h"
#include "SARibbonPanel.h"

namespace ps::plotting
{
QPixmap plotThumb(const QString &kind)
{
    QPixmap pm(76, 38);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(ThemeManager::adaptColor(QColor("#2d3841")), 1));
    p.setBrush(ThemeManager::adaptColor(QColor("#0b0f13")));
    p.drawRoundedRect(QRectF(0.5, 0.5, 75, 37), 3, 3);
    const QRectF r(7, 6, 62, 26);
    p.setPen(QPen(ThemeManager::adaptColor(QColor("#26313a")), 1));
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.topLeft());

    if (kind == QLatin1String("timeseries"))
    {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i)
        {
            const double x = r.left() + r.width() * i / 40.0;
            const double y = r.center().y() - std::sin(i * 0.28) * 8.0;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#4ea1ff")), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("xy"))
    {
        for (int i = 0; i < 9; ++i)
        {
            const double x = r.left() + 6 + (i * 13) % 50;
            const double y = r.bottom() - 3 - (i * 7) % 20;
            p.setPen(QPen(ThemeManager::adaptColor(QColor("#ffb454")), 1));
            p.setBrush(ThemeManager::adaptColor(QColor("#ffb454")));
            p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
    }
    else if (kind == QLatin1String("scatter"))
    {
        for (int i = 0; i < 14; ++i)
        {
            const double x = r.left() + 5 + (i * 17) % 52;
            const double y = r.bottom() - 2 - (i * 11) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(i % 3 ? ThemeManager::adaptColor(QColor("#7ee787")) : ThemeManager::adaptColor(QColor("#ffb454")));
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
    }
    else if (kind == QLatin1String("map"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#26313a")), 1));
        for (int i = 1; i < 4; ++i)
        {
            p.drawLine(QPointF(r.left() + r.width() * i / 4.0, r.top()),
                       QPointF(r.left() + r.width() * i / 4.0, r.bottom()));
            p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4.0),
                       QPointF(r.right(), r.top() + r.height() * i / 4.0));
        }
        QPainterPath path;
        path.moveTo(r.left() + 6, r.bottom() - 4);
        path.cubicTo(r.left() + 20, r.top() + 2, r.left() + 34, r.bottom() - 6, r.right() - 4, r.top() + 6);
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#4ea1ff")), 1.5));
        p.drawPath(path);
        p.setBrush(ThemeManager::adaptColor(QColor("#ff7b72")));
        p.drawEllipse(QPointF(r.right() - 4, r.top() + 6), 2.2, 2.2);
    }
    else if (kind == QLatin1String("bar"))
    {
        for (int i = 0; i < 5; ++i)
        {
            const double h = 4 + (i * 7) % 19;
            p.setPen(Qt::NoPen);
            p.setBrush(ThemeManager::adaptColor(QColor("#7ee787")));
            p.drawRect(QRectF(r.left() + 5 + i * 11, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("histogram"))
    {
        for (int i = 0; i < 7; ++i)
        {
            const double h = 3 + (i * 5 + i * i) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(ThemeManager::adaptColor(QColor("#d2a8ff")));
            p.drawRect(QRectF(r.left() + 4 + i * 8, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("fft"))
    {
        QPainterPath path;
        for (int i = 0; i <= 32; ++i)
        {
            const double x = r.left() + r.width() * i / 32.0;
            const double env = std::exp(-i * 0.12);
            const double y = r.bottom() - 2 - std::fabs(std::sin(i * 0.55)) * 18.0 * env;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#39c5cf")), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("traj3d"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#26313a")), 1));
        p.drawLine(QPointF(12, 28), QPointF(32, 21));
        p.drawLine(QPointF(32, 21), QPointF(58, 27));
        p.drawLine(QPointF(58, 27), QPointF(58, 9));
        p.drawLine(QPointF(58, 9), QPointF(36, 8));
        p.drawLine(QPointF(36, 8), QPointF(12, 14));
        p.drawLine(QPointF(12, 14), QPointF(12, 28));
        QPainterPath path;
        path.moveTo(14, 25);
        path.cubicTo(24, 8, 40, 12, 54, 20);
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#f2b134")), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("surface3d"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#2a3b48")), 1));
        for (int i = 0; i < 6; ++i)
        {
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(22 + i * 9, 31));
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(34 + i * 9, 9));
        }
        QPainterPath path;
        path.moveTo(12, 22);
        path.cubicTo(26, 8, 44, 27, 64, 13);
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#39c5cf")), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("horizon"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#4a5a68")), 1));
        p.setBrush(ThemeManager::adaptColor(QColor("#2b3a4a")));
        p.drawEllipse(QPointF(38, 19), 14, 14);
        p.setBrush(ThemeManager::adaptColor(QColor("#3a2f22")));
        p.drawChord(QRectF(24, 5, 28, 28), 180 * 16, 180 * 16);
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#f2b134")), 1.4));
        p.drawLine(QPointF(25, 19), QPointF(51, 19));
        p.drawLine(QPointF(38, 17), QPointF(38, 21));
        p.drawLine(QPointF(31, 19), QPointF(45, 19));
    }
    else if (kind == QLatin1String("compass"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#4a5a68")), 1));
        p.drawEllipse(QPointF(38, 19), 13, 13);
        p.drawText(QRectF(35, 5, 8, 8), Qt::AlignCenter, QStringLiteral("N"));
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#ff7b72")), 1.8));
        p.drawLine(QPointF(38, 19), QPointF(43, 10));
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#c9d1d9")), 1));
        p.drawLine(QPointF(38, 19), QPointF(34, 27));
    }
    else if (kind == QLatin1String("gauge"))
    {
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#4a5a68")), 1.2));
        p.drawArc(QRectF(18, 7, 40, 34), 200 * 16, -220 * 16);
        p.setPen(QPen(ThemeManager::adaptColor(QColor("#f2b134")), 1.6));
        p.drawLine(QPointF(38, 23), QPointF(51, 13));
        p.setBrush(ThemeManager::adaptColor(QColor("#f2b134")));
        p.drawEllipse(QPointF(38, 23), 2, 2);
    }
    return pm;
}

class LayoutPickGrid : public QWidget
{
public:
    explicit LayoutPickGrid(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);
        setFixedSize(8 * kStep + 2 * kPad, 8 * kStep + 2 * kPad);
    }

    std::function<void(int, int)> onPick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter pr(this);
        pr.fillRect(rect(), ThemeManager::color(QStringLiteral("panelAlt")));
        for (int r = 1; r <= 8; ++r)
        {
            for (int c = 1; c <= 8; ++c)
            {
                const QRect cell(kPad + (c - 1) * kStep, kPad + (r - 1) * kStep, 16, 16);
                const bool on = r <= m_rows && c <= m_cols;
                pr.fillRect(cell, on ? ThemeManager::color(QStringLiteral("accent")) : ThemeManager::color(QStringLiteral("line")));
                pr.setPen(on ? ThemeManager::color(QStringLiteral("accent")) : ThemeManager::color(QStringLiteral("textDim")));
                pr.drawRect(cell.adjusted(0, 0, -1, -1));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int c = std::clamp((e->pos().x() - kPad) / kStep + 1, 0, 8);
        const int r = std::clamp((e->pos().y() - kPad) / kStep + 1, 0, 8);
        if (r != m_rows || c != m_cols)
        {
            m_rows = r;
            m_cols = c;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_rows = m_cols = 0;
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || m_rows < 1 || m_cols < 1)
            return;
        if (onPick)
            onPick(m_rows, m_cols);
    }

private:
    static constexpr int kPad = 4;
    static constexpr int kStep = 18;
    int m_rows = 0;
    int m_cols = 0;
};


// 行×列缩略图：线框 + 分隔线，和 ribbon 其他线性图标风格一致
QPixmap layoutThumb(int rows, int cols)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QColor line = ThemeManager::adaptColor(QColor("#f2b134"));
    const QRectF area(3.5, 5.5, 25, 21);
    p.setPen(QPen(line, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area);
    p.setPen(QPen(line, 1.0));
    for (int r = 1; r < rows; ++r)
    {
        const qreal y = area.top() + area.height() * r / rows;
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    for (int c = 1; c < cols; ++c)
    {
        const qreal x = area.left() + area.width() * c / cols;
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    return pm;
}

// 布局选择弹层：独立 QFrame(Popup)。不再走 QToolButton::setMenu，
// 按钮上就不会再被 Qt 画出第二个菜单箭头
class LayoutPopup : public QFrame
{
public:
    explicit LayoutPopup(QWidget *parent) : QFrame(parent, Qt::Popup)
    {
        setObjectName(QStringLiteral("layoutPopup"));
        setStyleSheet(ThemeManager::mapStyleSheet(QStringLiteral(
            "QFrame#layoutPopup{background:#1b232b;border:1px solid #3a4650;}")));
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        m_pick = new LayoutPickGrid(this);
        lay->addWidget(m_pick);
    }
    LayoutPickGrid *pick() const { return m_pick; }

private:
    LayoutPickGrid *m_pick = nullptr;
};

void buildLayoutPicker(QWidget *p)
{
    auto *panel = static_cast<SARibbonPanel *>(p);
    auto *mw = qobject_cast<MainWindow *>(panel->window());
    if (!mw)
        return;

    // 用 ribbon 原生大按钮：尺寸由 SARibbonPanel 统一分配，不会压到面板分组标题
    auto *act = new QAction(layoutThumb(2, 1), QStringLiteral("2×1"), panel);
    act->setToolTip(QStringLiteral("点击选择行×列，重排当前页画布"));
    panel->addLargeAction(act);

    auto *popup = new LayoutPopup(mw);
    popup->pick()->onPick = [mw, act, popup](int r, int c) {
        act->setIcon(layoutThumb(r, c));
        act->setText(QStringLiteral("%1×%2").arg(r).arg(c));
        mw->commands().invoke(QStringLiteral("page.grid"),
                              ps::commands::CommandContext{mw, QStringLiteral("%1,%2").arg(r).arg(c)});
        popup->hide();
    };
    QObject::connect(act, &QAction::triggered, mw, [mw, panel, popup] {
        popup->adjustSize();
        const QRect avail = mw->screen()->availableGeometry();
        const QPoint anchor = panel->mapToGlobal(QPoint(0, panel->height()));
        int x = qBound(avail.left() + 8, anchor.x(), avail.right() - popup->width() - 8);
        int y = anchor.y() + 6;
        if (y + popup->height() > avail.bottom())
            y = anchor.y() - popup->height() - 6;
        popup->move(x, y);
        popup->show();
    });
}

namespace
{
QIcon ribIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}
} // namespace

// 页面：新建页大按钮 + 添加/删除/左右/上下四个小按钮两行排，都作用于当前画布
void buildPagePanel(QWidget *p)
{
    auto *panel = static_cast<SARibbonPanel *>(p);
    auto *mw = qobject_cast<MainWindow *>(panel->window());
    if (!mw)
        return;
    panel->setPanelLayoutMode(SARibbonPanel::TwoRowMode);

    auto *newPage = new QAction(ribIcon(QStringLiteral("newpage")), QStringLiteral("新建页"), panel);
    newPage->setToolTip(QStringLiteral("新建一个绘图页签"));
    QObject::connect(newPage, &QAction::triggered, mw, [mw] {
        mw->commands().invoke(QStringLiteral("page.new"), ps::commands::CommandContext{mw, {}});
    });
    panel->addLargeAction(newPage);

    auto small = [&](const QString &text, const QString &iconName, const QString &cmd,
                     const QString &tip) {
        auto *a = new QAction(ribIcon(iconName), text, panel);
        a->setToolTip(tip);
        QObject::connect(a, &QAction::triggered, mw, [mw, cmd] {
            mw->commands().invoke(cmd, ps::commands::CommandContext{mw, {}});
        });
        panel->addSmallAction(a);
    };
    small(QStringLiteral("添加"), QStringLiteral("plus"), QStringLiteral("page.addCanvas"),
          QStringLiteral("在当前画布下方拆出新画布"));
    small(QStringLiteral("删除"), QStringLiteral("trash"), QStringLiteral("page.removeCanvas"),
          QStringLiteral("删除当前画布"));
    small(QStringLiteral("左右"), QStringLiteral("split_h"), QStringLiteral("page.splitHorizontal"),
          QStringLiteral("在当前画布右侧拆出新画布"));
    small(QStringLiteral("上下"), QStringLiteral("split_v"), QStringLiteral("page.splitVertical"),
          QStringLiteral("在当前画布下方拆出新画布"));
}

// 显示：网格/边框/图例开关，两行排
void buildDisplayPanel(QWidget *p)
{
    auto *panel = static_cast<SARibbonPanel *>(p);
    auto *mw = qobject_cast<MainWindow *>(panel->window());
    if (!mw)
        return;
    panel->setPanelLayoutMode(SARibbonPanel::TwoRowMode);
    auto add = [&](const QString &text, const QString &iconName, int flag) {
        auto *a = new QAction(ribIcon(iconName), text, panel);
        a->setCheckable(true);
        a->setToolTip(text);
        QObject::connect(a, &QAction::toggled, mw,
                         [mw, flag](bool on) { ps::plotting::setCanvasViewFlag(mw, flag, on); });
        panel->addSmallAction(a);
    };
    add(QStringLiteral("网格"), QStringLiteral("grid"), 0);
    add(QStringLiteral("边框"), QStringLiteral("box"), 1);
    add(QStringLiteral("图例"), QStringLiteral("legend"), 2);
}

// 画布操作：放大/缩小/移动/自适应，两行排；放大缩小移动切换鼠标工具
void buildPanelsToolbar(QWidget *p)
{
    auto *panel = static_cast<SARibbonPanel *>(p);
    auto *mw = qobject_cast<MainWindow *>(panel->window());
    if (!mw)
        return;
    panel->setPanelLayoutMode(SARibbonPanel::TwoRowMode);

    auto *group = new QActionGroup(panel);
    group->setExclusive(true);
    auto toolAction = [&](const QString &text, const QString &iconName, int tool, const QString &tip) {
        auto *a = new QAction(ribIcon(iconName), text, panel);
        a->setCheckable(true);
        a->setToolTip(tip);
        group->addAction(a);
        QObject::connect(a, &QAction::toggled, mw, [mw, tool](bool on) {
            if (on)
                mw->commands().invoke(QStringLiteral("page.tool"),
                                      ps::commands::CommandContext{mw, QString::number(tool)});
        });
        panel->addSmallAction(a);
        return a;
    };
    auto *pan = toolAction(QStringLiteral("移动"), QStringLiteral("hand"), 0,
                           QStringLiteral("手型指针：按住左键拖动平移画布"));
    toolAction(QStringLiteral("放大"), QStringLiteral("zoom_in"), 1,
               QStringLiteral("放大镜指针：框选放大，滚轮缩放"));
    toolAction(QStringLiteral("缩小"), QStringLiteral("zoom_out"), 2,
               QStringLiteral("缩小镜指针：框选缩小"));
    pan->setChecked(true);

    auto *fit = new QAction(ribIcon(QStringLiteral("autoy")), QStringLiteral("自适应"), panel);
    fit->setToolTip(QStringLiteral("纵轴自适应到数据范围"));
    QObject::connect(fit, &QAction::triggered, mw,
                     [mw] { ps::plotting::canvasAutoY(mw); });
    panel->addSmallAction(fit);
}

void buildPlotGallery(QWidget *p)
{
    auto *panel = static_cast<SARibbonPanel *>(p);
    auto *mw = qobject_cast<MainWindow *>(panel->window());
    if (!mw)
        return;
    panel->setExpanding(false);
    panel->setMaximumWidth(400);
    if (auto *t = panel->titleLabel())
        t->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto run = [mw](const QString &cmd, const QString &arg = QString()) {
        return [mw, cmd, arg] { mw->commands().invoke(cmd, ps::commands::CommandContext{mw, arg}); };
    };
    QList<QAction *> curveActs, mapActs, gaugeActs;
    QHash<QString, QAction *> plotActByKind;
    auto addPlot = [&](const QString &kind, const QString &label, const QString &tip,
                       QList<QAction *> &dest) {
        auto *a = new QAction(QIcon(plotThumb(kind)), label, panel);
        a->setToolTip(tip);
        plotActByKind.insert(kind, a);
        QObject::connect(a, &QAction::triggered, mw, run(QStringLiteral("plot.new"), kind));
        dest.append(a);
    };
    addPlot(QStringLiteral("timeseries"), QStringLiteral("时间序列"), QStringLiteral("时间轴曲线，当前已实现"), curveActs);
    addPlot(QStringLiteral("xy"), QStringLiteral("XY 曲线"), QStringLiteral("X-Y 双通道曲线"), curveActs);
    addPlot(QStringLiteral("scatter"), QStringLiteral("散点图"), QStringLiteral("二维散点"), curveActs);
    addPlot(QStringLiteral("bar"), QStringLiteral("柱状图"), QStringLiteral("离散量柱状对比"), curveActs);
    addPlot(QStringLiteral("histogram"), QStringLiteral("直方图"), QStringLiteral("样本分布"), curveActs);
    addPlot(QStringLiteral("fft"), QStringLiteral("频谱 FFT"), QStringLiteral("幅值频谱"), curveActs);
    addPlot(QStringLiteral("traj3d"), QStringLiteral("3D 轨迹"), QStringLiteral("三维空间轨迹"), mapActs);
    addPlot(QStringLiteral("map"), QStringLiteral("GPS 地图"), QStringLiteral("经纬度轨迹与网格"), mapActs);
    addPlot(QStringLiteral("surface3d"), QStringLiteral("3D 曲面"), QStringLiteral("三维曲面 / 频谱"), mapActs);
    addPlot(QStringLiteral("horizon"), QStringLiteral("地平仪"), QStringLiteral("姿态地平仪"), gaugeActs);
    addPlot(QStringLiteral("compass"), QStringLiteral("罗盘"), QStringLiteral("航向罗盘"), gaugeActs);
    addPlot(QStringLiteral("gauge"), QStringLiteral("表盘"), QStringLiteral("单值仪表盘"), gaugeActs);
    QList<QAction *> commonActs;
    for (const char *k : {"timeseries", "xy", "scatter", "traj3d", "map"})
        if (auto *a = plotActByKind.value(QString::fromLatin1(k)))
            commonActs.append(a);
    // 单行图库 + 框内箭头，箭头弹出全部分类；和数据 ribbon 同一种外观
    auto *gallery = new ps::ui::RibbonGallery(panel);
    gallery->setPrimaryActions(commonActs);
    gallery->addGroup(QStringLiteral("曲线"), curveActs);
    gallery->addGroup(QStringLiteral("地图 / 3D"), mapActs);
    gallery->addGroup(QStringLiteral("仪表"), gaugeActs);
    panel->addWidget(gallery, SARibbonPanelItem::Large);
}

} // namespace ps::plotting
