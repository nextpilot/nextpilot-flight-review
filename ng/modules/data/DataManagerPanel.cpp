#include "DataManagerPanel.h"

#include "PanelFactoryRegistry.h"
#include "ThemeManager.h"
#include "ChannelMime.h"

#include "AppContext.h"
#include "Dataset.h"
#include "DataSource.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PropertySchema.h"
#include "TransformEditorDialog.h"
#include "FilterEditorDialog.h"

#include <QAbstractItemView>
#include <QCursor>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <functional>
#include <QMimeData>
#include <QStyledItemDelegate>
#include <QPolygonF>
#include <QIcon>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QTreeWidget>
#include <QPalette>
#include <QPainter>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

namespace
{
constexpr int kRoleDatasetId = Qt::UserRole + 1;
constexpr int kRoleTable = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
constexpr int kRoleSourceId = Qt::UserRole + 4;
constexpr int kRoleNodeKind = Qt::UserRole + 5;      // "source"/"dataset"/"table"/"column"/"param"/"msg"
constexpr int kRoleSourceActive = Qt::UserRole + 6;   // 源是否为当前激活（画左橙条）
constexpr int kRoleSourceKind = Qt::UserRole + 7;     // 源种类："file"/"live"…（live 画暂停/继续图标）

QString stateText(SourceState state)
{
    switch (state)
    {
    case SourceState::Opening:  return QStringLiteral("打开中");
    case SourceState::Streaming: return QStringLiteral("运行中");
    case SourceState::Paused:   return QStringLiteral("已暂停");
    case SourceState::Closed:   return QStringLiteral("已加载");
    case SourceState::Error:    return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

// 数据源行卡片底色：比树底色略深，明暗主题自适应。
// 取 base 向 text 混 6%——浅色 #FFFFFF→浅灰、深色 #25262A→深灰，均清晰可辨。
static QColor sourceCardColor(const QPalette &pal)
{
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const auto mix = [](int a, int b) { return a + (b - a) * 6 / 100; };
    return QColor(mix(base.red(), text.red()), mix(base.green(), text.green()),
                  mix(base.blue(), text.blue()));
}

// 源行卡片自绘（对齐老界面：40px 高、底色区分、激活左橙条、名称12px+摘要10px）。
// 只接管 kind=="source" 的节点，列/表节点交回基类默认绘制，故拖拽/选中/展开不受影响。
class SourceRowDelegate : public QStyledItemDelegate
{
public:
    explicit SourceRowDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    // 状态底色（对齐老界面 loaded绿/live青/off灰的半透明叠加）；stateText 为 UI 文案。
    static QColor stateTint(const QString &stateText, const QPalette &pal)
    {
        const bool dark = pal.color(QPalette::Base).lightness() < 128;
        if (stateText == QStringLiteral("运行中"))
            return QColor(dark ? QColor(57, 197, 207) : QColor(57, 197, 207));   // 青
        if (stateText == QStringLiteral("已加载") || stateText == QStringLiteral("已暂停"))
            return QColor(126, 231, 135);                                        // 绿
        if (stateText == QStringLiteral("错误"))
            return QColor(229, 72, 77);                                          // 红
        return QColor(110, 125, 136);                                            // 灰/离线
    }

    // 右侧操作图标（配置/移除），静态加载一次。
    static const QIcon &settingsIcon()
    {
        static const QIcon ic(QStringLiteral(":/icons/settings.svg"));
        return ic;
    }
    static const QIcon &trashIcon()
    {
        static const QIcon ic(QStringLiteral(":/icons/trash.svg"));
        return ic;
    }
    static const QIcon &playIcon()
    {
        static const QIcon ic(QStringLiteral(":/icons/play.svg"));
        return ic;
    }
    static const QIcon &pauseIcon()
    {
        static const QIcon ic(QStringLiteral(":/icons/pause.svg"));
        return ic;
    }

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        if (index.data(kRoleNodeKind).toString() != QStringLiteral("source"))
            return QStyledItemDelegate::sizeHint(opt, index);
        QSize s = QStyledItemDelegate::sizeHint(opt, index);
        s.setHeight(qMax(s.height(), 40));
        return s;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        if (index.data(kRoleNodeKind).toString() != QStringLiteral("source"))
        {
            QStyledItemDelegate::paint(p, opt, index);
            return;
        }
        const bool active = index.data(kRoleSourceActive).toBool();
        const QPalette &pal = opt.palette;
        const QColor card = sourceCardColor(pal);
        const QRect r = opt.rect;
        // 顶层项 rect 从缩进区后开始（≈17px）；老界面卡片贴面板左边缘，故左边界取 0。
        // 右边界取视口宽（item rect.right() 可能超出可视区，图标会被画到面板外）。
        auto *view = qobject_cast<const QTreeWidget *>(parent());
        const int cardRight = (view ? view->viewport()->width() : r.right()) - 1;
        const int cardLeft = 0;

        p->save();
        // 整行卡片底色：从视口最左铺到行尾，盖过缩进区（选中态用高亮色）
        p->fillRect(QRect(cardLeft, r.top(), cardRight - cardLeft, r.height()),
                    opt.state & QStyle::State_Selected
                        ? pal.color(QPalette::Highlight).lighter(120)
                        : card);
        // 状态底色叠加（对齐老界面 loaded绿/live青/off灰半透明）
        if (!(opt.state & QStyle::State_Selected))
        {
            QColor tint = stateTint(index.data(Qt::UserRole).toString(), pal);
            tint.setAlpha(26); // ~10% 强度
            p->fillRect(QRect(cardLeft, r.top(), cardRight - cardLeft, r.height()), tint);
        }
        // hover 高亮（老界面 QWidget:hover）
        if (opt.state & QStyle::State_MouseOver)
        {
            QColor hover = pal.color(QPalette::Text);
            hover.setAlpha(14);
            p->fillRect(QRect(cardLeft, r.top(), cardRight - cardLeft, r.height()), hover);
        }
        // 激活源左侧橙色竖条（老界面 #f2b134），贴最左
        if (active)
            p->fillRect(QRect(cardLeft, r.top(), 3, r.height()), QColor(QStringLiteral("#f2b134")));
        // 底部分隔线
        p->setPen(pal.color(QPalette::Mid));
        p->drawLine(cardLeft, r.bottom(), cardRight, r.bottom());

        // 自绘展开箭头（原生箭头位于缩进区、已被卡片底色覆盖）：橙条右侧
        const bool expanded = view && view->isExpanded(index);
        const bool hasChildren = index.model()->hasChildren(index);
        const int arrowX = cardLeft + 9;
        const int arrowY = r.top() + r.height() / 2;
        if (hasChildren)
        {
            p->setBrush(pal.color(QPalette::Text));
            p->setPen(Qt::NoPen);
            if (expanded)
                p->drawPolygon(QPolygonF({QPointF(arrowX - 3, arrowY - 2),
                                          QPointF(arrowX + 4, arrowY - 2),
                                          QPointF(arrowX, arrowY + 3)}));
            else
                p->drawPolygon(QPolygonF({QPointF(arrowX - 2, arrowY - 4),
                                          QPointF(arrowX - 2, arrowY + 4),
                                          QPointF(arrowX + 4, arrowY)}));
        }

        // 两行文字：名称（12px，正常色）+ 摘要（10px，dim）
        const QString text = index.data(Qt::DisplayRole).toString();
        const int sep = text.indexOf(QLatin1Char('\n'));
        const QString name = sep < 0 ? text : text.left(sep);
        const QString meta = sep < 0 ? QString() : text.mid(sep + 1);
        // 右侧操作图标区（live：暂停/继续 + 配置 + 移除；文件：配置 + 移除），文字避开它
        const bool live = index.data(kRoleSourceKind).toString() == QStringLiteral("live");
        const int iconZone = live ? 70 : 48;
        const int left = cardLeft + (hasChildren ? 22 : 8);
        const int textW = cardRight - left - iconZone;
        const int nameH = 16;
        QFont nameFont = opt.font;
        nameFont.setPixelSize(12);
        QFont metaFont = opt.font;
        metaFont.setPixelSize(10);
        p->setFont(nameFont);
        p->setPen(pal.color(QPalette::Text));
        p->drawText(QRect(left, r.top() + 2, textW, nameH),
                    Qt::AlignLeft | Qt::AlignVCenter | Qt::TextElideMode::ElideRight, name);
        if (!meta.isEmpty())
        {
            p->setFont(metaFont);
            p->setPen(pal.color(QPalette::Text).lighter(155));
            p->drawText(QRect(left, r.top() + 2 + nameH, textW, 14),
                        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextElideMode::ElideRight, meta);
        }

        // 右侧操作图标：live 源在配置/移除前多一个暂停/继续（运行中画暂停、已暂停画播放）；
        // 文件源保持老界面两图标布局。
        const int iconY = r.top() + (r.height() - 16) / 2;
        if (live)
        {
            const bool paused = index.data(Qt::UserRole).toString() == QStringLiteral("已暂停");
            (paused ? playIcon() : pauseIcon()).paint(p, QRect(cardRight - iconZone + 4, iconY, 16, 16));
            settingsIcon().paint(p, QRect(cardRight - iconZone + 26, iconY, 16, 16));
            trashIcon().paint(p, QRect(cardRight - iconZone + 48, iconY, 16, 16));
        }
        else
        {
            settingsIcon().paint(p, QRect(cardRight - iconZone + 4, iconY, 16, 16));
            trashIcon().paint(p, QRect(cardRight - iconZone + 26, iconY, 16, 16));
        }
        p->restore();
    }
};

// 源卡片右侧操作命中：对齐老界面配置/移除按钮。
// 命中区与 SourceRowDelegate::paint 的图标绘制坐标一致。
enum class SourceRowAction { Config, Remove, PauseResume };

// 列节点可拖拽：只发逻辑标识（ChannelMime），不暴露 Dataset 内部结构。
class ChannelTreeWidget : public QTreeWidget
{
public:
    explicit ChannelTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent) {}

    // 点击源卡片右侧图标时回调（sourceId, action）；由面板接线到配置/移除。
    std::function<void(int, SourceRowAction)> onRowAction;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onRowAction)
        {
            QTreeWidgetItem *item = itemAt(e->pos());
            if (item && item->data(0, kRoleNodeKind).toString() == QStringLiteral("source"))
            {
                const QRect row = visualRect(indexFromItem(item));
                const int cardRight = viewport()->width() - 1;
                const bool live = item->data(0, kRoleSourceKind).toString() == QStringLiteral("live");
                const int iconZone = live ? 70 : 48;
                const int iconY = row.top() + (row.height() - 16) / 2;
                const QRect pauseRect(cardRight - iconZone + 4, iconY, 16, 16);
                const QRect cfgRect(cardRight - iconZone + 26, iconY, 16, 16);
                const QRect delRect(cardRight - iconZone + 48, iconY, 16, 16);
                const int sid = item->data(0, kRoleSourceId).toInt();
                if (live && pauseRect.contains(e->pos()))
                {
                    onRowAction(sid, SourceRowAction::PauseResume);
                    return; // 吞掉，避免同时选中/展开
                }
                if (!live)
                {
                    // 文件源两图标口径：配置在 +4，移除在 +26
                    const QRect cfgRectF(cardRight - iconZone + 4, iconY, 16, 16);
                    const QRect delRectF(cardRight - iconZone + 26, iconY, 16, 16);
                    if (cfgRectF.contains(e->pos()))
                    {
                        onRowAction(sid, SourceRowAction::Config);
                        return;
                    }
                    if (delRectF.contains(e->pos()))
                    {
                        onRowAction(sid, SourceRowAction::Remove);
                        return;
                    }
                }
                else
                {
                    if (cfgRect.contains(e->pos()))
                    {
                        onRowAction(sid, SourceRowAction::Config);
                        return;
                    }
                    if (delRect.contains(e->pos()))
                    {
                        onRowAction(sid, SourceRowAction::Remove);
                        return;
                    }
                }
            }
        }
        QTreeWidget::mousePressEvent(e);
    }

    void startDrag(Qt::DropActions supportedActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item)
            return;
        const QVariant dsId = item->data(0, kRoleDatasetId);
        const QVariant table = item->data(0, kRoleTable);
        const QVariant column = item->data(0, kRoleColumn);
        if (!dsId.isValid() || !table.isValid() || !column.isValid())
            return;
        auto *mime = new QMimeData;
        const QString payload = channel_mime::encode(dsId.toInt(), table.toString(), column.toString());
        mime->setData(QString::fromLatin1(channel_mime::type()), payload.toUtf8());
        mime->setText(table.toString() + QLatin1Char('.') + column.toString());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(supportedActions, Qt::CopyAction);
    }
};

struct DeriveRecipe
{
    const char *id;
    const char *label;
    const char *params;
};
const DeriveRecipe kDeriveRecipes[] = {
    {"absolute",       "绝对值",     "{}"},
    {"normalize",      "归一化",     "{}"},
    {"scale",          "缩放/偏移",  "{\"value_scale\":1,\"value_offset\":0}"},
    {"derivative",     "微分",       "{}"},
    {"integral",       "积分",       "{}"},
    {"moving_average", "滑动平均",   "{\"window\":10}"},
    {"moving_rms",     "滑动均方根", "{\"window\":10}"},
    {"moving_variance","滑动方差",   "{\"window\":10}"},
};

bool isKeyValueTable(const Table *t)
{
    if (!t || t->rows <= 0 || t->columns.isEmpty())
        return false;
    for (const Column &c : t->columns)
        if (c.type == ValueType::Text)
            return true;
    return false;
}

} // namespace

DataManagerPanel::DataManagerPanel(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_repo(appCtx.data()), m_sources(appCtx.sources()),
      m_executor(executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 标题行：左「数据源」+ 右「N源 · N已连接」，对齐老界面两段式。
    auto *headerRow = new QWidget(this);
    auto *headerLay = new QHBoxLayout(headerRow);
    headerLay->setContentsMargins(8, 6, 8, 4);
    headerLay->setSpacing(6);
    m_header = new QLabel(QStringLiteral("数据源"), headerRow);
    m_header->setObjectName(QStringLiteral("sectionTitle"));
    headerLay->addWidget(m_header);
    headerLay->addStretch(1);
    m_srcCount = new QLabel(QStringLiteral("0 源 · 0 已连接"), headerRow);
    m_srcCount->setObjectName(QStringLiteral("textDim"));
    headerLay->addWidget(m_srcCount);
    lay->addWidget(headerRow);

    // chip：字段 / 参数 / 消息（状态模型；对齐老界面——老 Dock chipHost->hide()，
    // 默认只显示字段视图，参数/消息视图保留为隐藏能力，不暴露切换入口）。
    auto *chipHost = new QWidget(this);
    auto *chipLay = new QHBoxLayout(chipHost);
    chipLay->setContentsMargins(6, 2, 6, 2);
    chipLay->setSpacing(4);
    const QString chipQss = QStringLiteral(
        "QToolButton#sourceChip{background:palette(button);border:1px solid palette(mid);"
        "border-radius:9px;color:palette(button-text);font-size:11px;padding:2px 8px;}"
        "QToolButton#sourceChip:hover{border-color:palette(highlight);}"
        "QToolButton#sourceChip:checked{background:palette(highlight);color:palette(highlighted-text);"
        "border-color:palette(highlight);}");
    const char *chipLabels[3] = {"字段 0", "参数 0", "消息 0"};
    for (int i = 0; i < 3; ++i)
    {
        auto *chip = new QToolButton(chipHost);
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        chip->setText(QString::fromLatin1(chipLabels[i]));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setObjectName(QStringLiteral("sourceChip"));
        chip->setStyleSheet(chipQss);
        chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chip->setProperty("chipIndex", i);
        m_chips[i] = chip;
        chipLay->addWidget(chip, 1);
        connect(chip, &QToolButton::clicked, this, [this, i] { selectChip(i); });
    }
    chipHost->hide(); // 对齐老界面：三分类切换不暴露，默认字段视图。
    lay->addWidget(chipHost);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setObjectName(QStringLiteral("searchBox"));
    m_filter->setFixedHeight(30); // 对齐老界面 m_srcFilter->setFixedHeight(30)
    // 对齐老界面：搜索框左侧带搜索图标（LeadingPosition）。
    m_filter->addAction(QIcon(QStringLiteral(":/icons/search.svg")), QLineEdit::LeadingPosition);
    // 对齐老界面：搜索框与数据源树左右边缘齐平，不要额外内边距。
    lay->addWidget(m_filter);
    connect(m_filter, &QLineEdit::textChanged, this, &DataManagerPanel::applyFilter);

    // 统一数据源树：每行一个源，展开后直接是表 → 列（dataset 层在 UI 透明化）。
    m_tree = new ChannelTreeWidget(this);
    // 对齐老界面：数据源树无横向表头（老 Dock 的 QTreeWidget 不显示 header）。
    // 状态摘要并入源名文字（见 rebuildTree），不依赖第 2 列，故只需单列 Stretch。
    m_tree->header()->hide();
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setItemDelegate(new SourceRowDelegate(m_tree)); // 源行卡片自绘
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setDefaultDropAction(Qt::CopyAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(14);
    // 源卡片右侧齿轮/垃圾桶/暂停点击：配置弹说明（文件源）/移除/live 暂停继续。
    static_cast<ChannelTreeWidget *>(m_tree)->onRowAction = [this](int sourceId, SourceRowAction action) {
        if (action == SourceRowAction::PauseResume)
        {
            const DataSourceInfo *li = m_sources.get(sourceId);
            if (!li || li->kind != SourceKind::Live)
                return;
            const bool paused = li->state == SourceState::Paused;
            Intent pint; pint.op = paused ? QStringLiteral("data.resume") : QStringLiteral("data.pause");
            pint.params[QStringLiteral("id")] = sourceId;
            const auto pr = m_executor.execute(pint);
            if (!pr)
                QMessageBox::warning(this, paused ? QStringLiteral("继续失败") : QStringLiteral("暂停失败"),
                                     pr.error().message);
            return; // 状态切换经 sourceChanged 信号刷新面板
        }
        if (action == SourceRowAction::Config)
        {
            const DataSourceInfo *info = m_sources.get(sourceId);
            QMessageBox::information(this, QStringLiteral("配置数据源"),
                QStringLiteral("「%1」是文件数据源，使用打开时的解析选项；\n如需更换解析方式，请移除后重新打开。")
                    .arg(info ? (info->name.isEmpty() ? info->origin : info->name) : QStringLiteral("该源")));
            return;
        }
        // Remove：确认后走 data.remove，注册表 sourceRemoved 信号会触发面板刷新。
        const DataSourceInfo *info = m_sources.get(sourceId);
        const QString name = info ? (info->name.isEmpty() ? QFileInfo(info->origin).fileName() : info->name)
                                  : QStringLiteral("该数据源");
        if (QMessageBox::question(this, QStringLiteral("移除数据源"),
                QStringLiteral("确定移除「%1」及其全部通道？").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        Intent intent; intent.op = QStringLiteral("data.remove");
        intent.params[QStringLiteral("id")] = sourceId;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("移除失败"), r.error().message);
    };
    lay->addWidget(m_tree, 1);

    // 空态：无源时显示，树隐藏。对齐老界面 m_srcEmpty：提示 + 打开日志/连接实时源/最近文件。
    m_empty = new QWidget(this);
    auto *emptyLay = new QVBoxLayout(m_empty);
    emptyLay->setAlignment(Qt::AlignCenter);
    auto *emptyHint = new QLabel(QStringLiteral("还没有数据源\n打开飞行日志，或连接一个数据源开始分析"), m_empty);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyHint->setObjectName(QStringLiteral("textDim"));
    emptyLay->addWidget(emptyHint);
    emptyLay->addSpacing(6);
    auto mkEmptyBtn = [this, emptyLay](const QString &text) {
        auto *b = new QPushButton(text, m_empty);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(28);
        b->setObjectName(QStringLiteral("emptyAction"));
        emptyLay->addWidget(b);
        return b;
    };
    auto *openBtn = mkEmptyBtn(QStringLiteral("打开日志"));
    auto *liveBtn = mkEmptyBtn(QStringLiteral("连接实时源"));
    auto *recentBtn = mkEmptyBtn(QStringLiteral("最近文件"));
    emptyLay->addStretch();
    connect(openBtn, &QPushButton::clicked, this, &DataManagerPanel::openLogFile);
    connect(liveBtn, &QPushButton::clicked, this, [this] {
        // NG 暂无实时源运行时：对齐老界面入口——列出支持的实时 transport 扩展，
        // 无可用扩展时给出明确提示，不留死按钮。
        QMessageBox::information(this, QStringLiteral("连接实时源"),
            QStringLiteral("当前版本的实时源采集尚未接入；可通过「打开日志」加载文件数据源。"));
    });
    connect(recentBtn, &QPushButton::clicked, this, [this] {
        showRecentFilesMenu(QCursor::pos());
    });
    lay->addWidget(m_empty, 1);

    // 树：双击列节点 = 激活通道；单击源节点 = 展开/选中；右键 = 上下文菜单。
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item)
            return;
        const QVariant dsId = item->data(0, kRoleDatasetId);
        const QVariant table = item->data(0, kRoleTable);
        const QVariant column = item->data(0, kRoleColumn);
        if (!dsId.isValid() || !table.isValid() || !column.isValid())
            return;
        const int datasetId = dsId.toInt();
        const QString tableName = table.toString();
        const QString columnName = column.toString();
        const int sourceId = m_sources.sourceOfDataset(datasetId);
        if (sourceId >= 0)
            m_appCtx.setActiveSourceId(sourceId);
        m_appCtx.setActiveChannel(datasetId, tableName, columnName);
        emit channelActivated(datasetId, tableName, columnName);
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int col) {
        if (!item || col != 0)
            return;
        // 统一选中：任何节点单击都上报，属性面板据此刷新。
        reportSelection(item);
        const QVariant sid = item->data(0, kRoleSourceId);
        if (!sid.isValid())
            return;
        const int sourceId = sid.toInt();
        if (sourceId != m_shownSourceId)
            selectSource(sourceId);
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item)
        {
            // 空白区右键：最近文件菜单（抄老代码空态）。
            showRecentFilesMenu(m_tree->viewport()->mapToGlobal(pos));
            return;
        }
        const QPoint global = m_tree->viewport()->mapToGlobal(pos);
        const QVariant sid = item->data(0, kRoleSourceId);
        if (sid.isValid() && item->data(0, kRoleNodeKind).toString() == QStringLiteral("source"))
            showSourceMenu(sid.toInt(), global);
        else if (item->data(0, kRoleDatasetId).isValid()
                 && item->data(0, kRoleTable).isValid()
                 && item->data(0, kRoleColumn).isValid())
            showColumnMenu(item, global);
    });

    connect(&m_repo, &DatasetRegistry::datasetAdded, this, [this](DatasetId) { refresh(); });
    connect(&m_repo, &DatasetRegistry::datasetRemoved, this, [this](DatasetId) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceAdded, this, [this](int) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceRemoved, this, [this](int) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceChanged, this, [this](int) { rebuildTree(); });

    // 对齐老界面：Ctrl+T 对当前选中列直接添加变换（derive）。
    auto *transformShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    connect(transformShortcut, &QShortcut::activated, this, [this] {
        QTreeWidgetItem *item = m_tree->currentItem();
        if (!item || !item->data(0, kRoleDatasetId).isValid()
            || !item->data(0, kRoleTable).isValid() || !item->data(0, kRoleColumn).isValid())
            return;
        deriveColumn(item, m_tree->viewport()->mapToGlobal(
            m_tree->visualItemRect(item).center()));
    });

    registerPropertyProviders();
    refresh();
}

// 单击树节点 -> 统一选中对象。把稳定标识填进 ids，供属性 provider 反查。
void DataManagerPanel::reportSelection(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString kind = item->data(0, kRoleNodeKind).toString();
    Selection sel;
    sel.ids.insert(QStringLiteral("source"), item->data(0, kRoleSourceId));
    sel.ids.insert(QStringLiteral("dataset"), item->data(0, kRoleDatasetId));
    sel.ids.insert(QStringLiteral("table"), item->data(0, kRoleTable));
    sel.ids.insert(QStringLiteral("column"), item->data(0, kRoleColumn));
    sel.label = item->text(0);

    if (kind == QStringLiteral("source"))
    {
        sel.kind = SelectionKind::Source;
        const int sid = item->data(0, kRoleSourceId).toInt();
        sel.ids.insert(QStringLiteral("sourceId"), sid);
    }
    else if (kind == QStringLiteral("dataset"))
        sel.kind = SelectionKind::Dataset;
    else if (kind == QStringLiteral("table"))
        sel.kind = SelectionKind::Table;
    else if (kind == QStringLiteral("column"))
        sel.kind = SelectionKind::Column;
    else if (kind == QStringLiteral("param"))
        sel.kind = SelectionKind::Param;
    else if (kind == QStringLiteral("msg"))
        sel.kind = SelectionKind::Message;
    else
        return; // 未知节点不上报
    m_appCtx.setSelection(sel);
}

// 注册数据源树各节点的属性提供者与写入器。
// provider 只查仓库生成 schema，不含任何 QWidget；setter 目前对数据源节点
// 主要是只读（写入留 placeholder，返回 false -> 面板走 prop.set 意图兜底）。
void DataManagerPanel::registerPropertyProviders()
{
    auto row = [](const QString &key, const QString &label, const QString &value,
                  PropKind kind = PropKind::ReadOnly) {
        PropRow r;
        r.key = key;
        r.label = label;
        r.value = value;
        r.kind = kind;
        r.editable = false;
        return r;
    };

    // --- 数据源 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Source, [this, row](const Selection &sel) {
        const int sid = sel.ids.value(QStringLiteral("sourceId"),
                                      sel.ids.value(QStringLiteral("source"))).toInt();
        const DataSourceInfo *info = m_sources.get(sid);
        PropertySchema s;
        s.title = sel.label.isEmpty() ? QStringLiteral("数据源") : sel.label;
        PropObject o;
        o.id = QStringLiteral("source");
        o.label = QStringLiteral("数据源");
        if (info)
        {
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), info->name);
            o.rows << row(QStringLiteral("origin"), QStringLiteral("来源路径"), info->origin);
            o.rows << row(QStringLiteral("format"), QStringLiteral("格式"), info->formatHint);
            o.rows << row(QStringLiteral("state"), QStringLiteral("状态"), stateText(info->state));
            o.rows << row(QStringLiteral("datasets"), QStringLiteral("数据集数"),
                          QString::number(info->datasetIds.size()));
        }
        else
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), sel.label);
        s.objects << o;
        return s.toJson();
    });

    // --- 数据集 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Dataset, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(dsId);
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("dataset");
        o.label = QStringLiteral("数据集");
        o.rows << row(QStringLiteral("id"), QStringLiteral("ID"), QString::number(dsId));
        if (ds)
        {
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), ds->name());
            o.rows << row(QStringLiteral("tables"), QStringLiteral("表数"), QString::number(ds->tableCount()));
        }
        s.objects << o;
        return s.toJson();
    });

    // --- 表 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Table, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const QString tableName = sel.ids.value(QStringLiteral("table")).toString();
        const Dataset *ds = m_repo.get(dsId);
        const Table *t = ds ? ds->table(tableName) : nullptr;
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("table");
        o.label = QStringLiteral("表");
        o.rows << row(QStringLiteral("name"), QStringLiteral("表名"), tableName);
        if (t)
        {
            o.rows << row(QStringLiteral("rows"), QStringLiteral("行数"), QString::number(t->rows));
            o.rows << row(QStringLiteral("columns"), QStringLiteral("列数"), QString::number(t->columns.size()));
            o.rows << row(QStringLiteral("span"), QStringLiteral("时间跨度(s)"),
                          QString::number((t->lastTimeUs - t->firstTimeUs) / 1000000.0, 'f', 3));
        }
        s.objects << o;
        return s.toJson();
    });

    // --- 列（字段）：展示列元信息；可编辑"变换"行由曲线/衍生场景承接 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Column, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const QString tableName = sel.ids.value(QStringLiteral("table")).toString();
        const QString columnName = sel.ids.value(QStringLiteral("column")).toString();
        const Dataset *ds = m_repo.get(dsId);
        const Table *t = ds ? ds->table(tableName) : nullptr;
        const Column *col = t ? t->column(columnName) : nullptr;
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("column");
        o.label = QStringLiteral("字段");
        o.rows << row(QStringLiteral("name"), QStringLiteral("列名"), columnName);
        o.rows << row(QStringLiteral("table"), QStringLiteral("所属表"), tableName);
        if (col)
        {
            o.rows << row(QStringLiteral("unit"), QStringLiteral("单位"), col->unit);
            o.rows << row(QStringLiteral("type"), QStringLiteral("类型"),
                          QString::number(static_cast<int>(col->type)));
            o.rows << row(QStringLiteral("rows"), QStringLiteral("行数"), QString::number(col->rowCount));
        }
        s.objects << o;
        PropObject hint;
        hint.id = QStringLiteral("actions");
        hint.label = QStringLiteral("操作");
        hint.rows << row(QStringLiteral("derive"), QStringLiteral("衍生"),
                         QStringLiteral("右键列 -> 添加变换（算子/表达式/Lua）"));
        s.objects << hint;
        return s.toJson();
    });

    // --- 参数 / 消息：占位只读（真实文本值待 Rust 文本读取槽，见架构文档）---
    auto kvProvider = [this, row](const Selection &sel) {
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("node");
        o.label = selectionKindName(sel.kind);
        o.rows << row(QStringLiteral("label"), QStringLiteral("名称"), sel.label);
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        o.rows << row(QStringLiteral("dataset"), QStringLiteral("数据集ID"), QString::number(dsId));
        s.objects << o;
        return s.toJson();
    };
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Param, kvProvider);
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Message, kvProvider);
}

void DataManagerPanel::selectSource(int sourceId)
{
    m_shownSourceId = sourceId;
    rebuildTree();
}

void DataManagerPanel::selectChip(int chip)
{
    if (chip == m_shownChip)
        return;
    m_shownChip = chip;
    rebuildTree();
}

void DataManagerPanel::rebuildChips()
{
    // 统计三类数量填充 chip 文本。
    int nFields = 0, nParams = 0, nMsgs = 0;
    m_paramCount = 0;
    m_msgCount = 0;
    for (int sid : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sid);
        if (!info)
            continue;
        for (int dsId : info->datasetIds)
        {
            const Dataset *ds = m_repo.get(dsId);
            if (!ds)
                continue;
            for (int t = 0; t < ds->tableCount(); ++t)
            {
                const Table *tbl = ds->table(t);
                if (!tbl || tbl->rows <= 0)
                    continue;
                if (isKeyValueTable(tbl))
                {
                    if (tbl->name.contains(QStringLiteral("param"), Qt::CaseInsensitive) ||
                        tbl->name.contains(QStringLiteral("fact"), Qt::CaseInsensitive))
                    { nParams += tbl->rows; ++m_paramCount; }
                    else
                    { nMsgs += tbl->rows; ++m_msgCount; }
                }
                else
                    nFields += tbl->columns.size();
            }
        }
    }
    m_chips[0]->setText(QStringLiteral("字段 %1").arg(nFields));
    m_chips[1]->setText(QStringLiteral("参数 %1").arg(nParams));
    m_chips[2]->setText(QStringLiteral("消息 %1").arg(nMsgs));
    for (int i = 0; i < 3; ++i)
        m_chips[i]->setChecked(i == m_shownChip);
}

void DataManagerPanel::rebuildTree()
{
    const bool blocked = m_tree->blockSignals(true);
    m_tree->clear();
    rebuildChips();

    const bool showFields = m_shownChip == 0;
    // 标题两段：左固定「数据源」，右「N源 · N已连接」（对齐老界面 refreshSrcCount）。
    if (m_srcCount)
    {
        int connected = 0;
        for (int sid : m_sources.ids())
        {
            const DataSourceInfo *ci = m_sources.get(sid);
            if (ci && ci->state != SourceState::Opening && ci->state != SourceState::Error)
                ++connected;
        }
        m_srcCount->setText(QStringLiteral("%1 源 · %2 已连接").arg(m_sources.count()).arg(connected));
    }

    for (int sid : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sid);
        if (!info)
            continue;

        auto *srcItem = new QTreeWidgetItem(m_tree);
        const QString srcName = info->name.isEmpty() ? QFileInfo(info->origin).fileName() : info->name;
        int chCount = 0;
        for (int dsId : info->datasetIds)
            if (const Dataset *ds = m_repo.get(dsId))
                for (int t = 0; t < ds->tableCount(); ++t)
                    if (!isKeyValueTable(ds->table(t)))
                        chCount += ds->table(t)->columns.size();
        // 对齐老界面：源名一行 + 下方 dim 摘要一行（不依赖被隐藏的表头第 2 列）。
        const QString srcMeta = QStringLiteral("%1 · %2 通道").arg(stateText(info->state)).arg(chCount);
        srcItem->setText(0, srcName + QLatin1Char('\n') + srcMeta);
        srcItem->setData(0, kRoleSourceId, sid);
        srcItem->setData(0, kRoleNodeKind, QStringLiteral("source"));
        const bool active = (sid == m_shownSourceId || m_shownSourceId < 0);
        srcItem->setData(0, kRoleSourceActive, active);
        srcItem->setData(0, kRoleSourceKind,
                         info->kind == SourceKind::Live  ? QStringLiteral("live")
                         : info->kind == SourceKind::Replay ? QStringLiteral("replay")
                         : info->kind == SourceKind::Stream ? QStringLiteral("stream")
                                                            : QStringLiteral("file"));
        srcItem->setData(0, Qt::UserRole, stateText(info->state)); // sourceStateText 用
        srcItem->setFirstColumnSpanned(true); // 卡片底色横跨整行

        if (showFields)
        {
            // 字段视图：dataset 层在 UI 透明化。收集该源全部字段表（跨 dataset）。
            struct FieldTable { int dsId; const Table *table; };
            QList<FieldTable> tables;
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *table = ds->table(t);
                    if (table && !isKeyValueTable(table))
                        tables.append({dsId, table});
                }
            }
            // 单表时省掉表节点（源 → 列）；多表时保留表节点分组（源 → 表 → 列）。
            const bool flatten = tables.size() == 1;
            for (const FieldTable &ft : tables)
            {
                QTreeWidgetItem *owner = srcItem;
                if (!flatten)
                {
                    auto *tItem = new QTreeWidgetItem(srcItem);
                    tItem->setText(0, ft.table->name);
                    const double span = (ft.table->lastTimeUs - ft.table->firstTimeUs) / 1000000.0;
                    tItem->setText(1, QStringLiteral("%1 行 · %2 s").arg(ft.table->rows).arg(span, 0, 'f', 2));
                    tItem->setData(0, kRoleTable, ft.table->name);
                    tItem->setData(0, kRoleDatasetId, ft.dsId);
                    tItem->setData(0, kRoleNodeKind, QStringLiteral("table"));
                    tItem->setToolTip(0, QStringLiteral("数据集 #%1").arg(ft.dsId));
                    tItem->setExpanded(true);
                    owner = tItem;
                }
                for (const Column &col : ft.table->columns)
                {
                    auto *cItem = new QTreeWidgetItem(owner);
                    cItem->setText(0, col.name);
                    cItem->setText(1, col.unit);
                    cItem->setData(0, kRoleColumn, col.name);
                    cItem->setData(0, kRoleTable, ft.table->name);
                    cItem->setData(0, kRoleDatasetId, ft.dsId);
                    cItem->setData(0, kRoleNodeKind, QStringLiteral("column"));
                    cItem->setFlags(cItem->flags() | Qt::ItemIsDragEnabled);
                    cItem->setToolTip(0, QStringLiteral("拖到画布即可绘制 %1.%2").arg(ft.table->name, col.name));
                }
            }
        }
        else if (m_shownChip == 1)
        {
            // 参数视图：每个 dataset 一个节点，下面是 param 表。
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *tbl = ds->table(t);
                    if (!tbl || !isKeyValueTable(tbl))
                        continue;
                    if (!(tbl->name.contains(QStringLiteral("param"), Qt::CaseInsensitive) ||
                          tbl->name.contains(QStringLiteral("fact"), Qt::CaseInsensitive)))
                        continue;
                    auto *pItem = new QTreeWidgetItem(srcItem);
                    pItem->setText(0, QStringLiteral("[%1] %2").arg(tbl->name).arg(
                        tbl->columns.isEmpty() ? QStringLiteral("—") : tbl->columns.first().name));
                    pItem->setText(1, QStringLiteral("%1 项").arg(tbl->rows));
                    pItem->setData(0, kRoleNodeKind, QStringLiteral("param"));
                    pItem->setData(0, kRoleDatasetId, dsId);
                    pItem->setFlags(pItem->flags() & ~Qt::ItemIsDragEnabled);
                }
            }
        }
        else
        {
            // 消息视图：每个非键值表一个消息节点。
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *tbl = ds->table(t);
                    if (!tbl || tbl->rows <= 0 || isKeyValueTable(tbl))
                        continue;
                    auto *mItem = new QTreeWidgetItem(srcItem);
                    mItem->setText(0, tbl->name);
                    mItem->setText(1, QStringLiteral("%1 字段 · %2 s").arg(tbl->columns.size())
                                          .arg(tbl->firstTimeUs / 1000000.0, 0, 'f', 2));
                    mItem->setData(0, kRoleNodeKind, QStringLiteral("msg"));
                    mItem->setData(0, kRoleDatasetId, dsId);
                    mItem->setFlags(mItem->flags() & ~Qt::ItemIsDragEnabled);
                }
            }
        }

        // 展开当前选中的源（老代码：同一时刻只展开一个源）。
        srcItem->setExpanded(sid == m_shownSourceId || m_shownSourceId < 0);
    }
    m_tree->blockSignals(blocked);
    applyFilter();
    updateEmptyState();
}

void DataManagerPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    const bool empty = needle.isEmpty();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *src = m_tree->topLevelItem(i);
        bool srcHit = false;
        for (int d = 0; d < src->childCount(); ++d)
        {
            QTreeWidgetItem *child = src->child(d);
            bool childHit = empty || child->text(0).contains(needle, Qt::CaseInsensitive);
            for (int c = 0; c < child->childCount(); ++c)
            {
                QTreeWidgetItem *leaf = child->child(c);
                const bool hit = empty || childHit || leaf->text(0).contains(needle, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                childHit = childHit || hit;
            }
            child->setHidden(!childHit);
            srcHit = srcHit || childHit;
        }
        // 源名命中也算（展开全部子项）。
        const bool nameHit = empty || src->text(0).contains(needle, Qt::CaseInsensitive);
        src->setHidden(!(srcHit || nameHit));
    }
}

void DataManagerPanel::updateEmptyState()
{
    const bool hasSource = m_sources.count() > 0;
    m_tree->setVisible(hasSource);
    m_empty->setVisible(!hasSource);
}

void DataManagerPanel::showSourceMenu(int sourceId, const QPoint &globalPos)
{
    const DataSourceInfo *info = m_sources.get(sourceId);
    if (!info)
        return;
    QMenu menu(this);
    // 实时源才给"暂停/继续"（文件源没有流可暂停，给了反而是假入口）。
    const bool live = info->kind == SourceKind::Live;
    const bool paused = info->state == SourceState::Paused;
    QAction *pause = live ? menu.addAction(paused ? QStringLiteral("继续接收") : QStringLiteral("暂停接收")) : nullptr;
    QAction *reload = menu.addAction(QStringLiteral("重载数据源"));
    QAction *rename = menu.addAction(QStringLiteral("重命名…"));
    QAction *exportCsv = menu.addAction(QStringLiteral("导出 CSV…"));
    QAction *clearCache = menu.addAction(QStringLiteral("清除全部解析缓存"));
    menu.addSeparator();
    QAction *detail = menu.addAction(QStringLiteral("数据源信息…"));
    QAction *activate = menu.addAction(QStringLiteral("设为活动数据源"));
    menu.addSeparator();
    QAction *remove = menu.addAction(QStringLiteral("移除数据源"));
    QAction *removeAll = menu.addAction(QStringLiteral("移除全部数据源"));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (live && chosen == pause)
    {
        Intent pint; pint.op = paused ? QStringLiteral("data.resume") : QStringLiteral("data.pause");
        pint.params[QStringLiteral("id")] = sourceId;
        const auto pr = m_executor.execute(pint);
        if (!pr)
            QMessageBox::warning(this, paused ? QStringLiteral("继续失败") : QStringLiteral("暂停失败"),
                                 pr.error().message);
        return;
    }
    if (chosen == rename)
    {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("重命名数据源"),
            QStringLiteral("显示名："), QLineEdit::Normal,
            info->name.isEmpty() ? QFileInfo(info->origin).fileName() : info->name, &ok);
        if (!ok)
            return;
        // 只改显示名，不动 origin；update() 会 emit sourceChanged 触发面板刷新。
        DataSourceInfo copy = *info;
        copy.name = text.trimmed();
        m_sources.update(sourceId, copy);
        return;
    }
    if (chosen == detail)
    {
        const QString kindText = info->kind == SourceKind::Live ? QStringLiteral("实时源")
                               : info->kind == SourceKind::Stream ? QStringLiteral("流")
                               : info->kind == SourceKind::Replay ? QStringLiteral("回放")
                                                                  : QStringLiteral("文件");
        const QString stateText = info->state == SourceState::Streaming ? QStringLiteral("接收中")
                                : info->state == SourceState::Paused ? QStringLiteral("已暂停")
                                : info->state == SourceState::Error ? QStringLiteral("错误")
                                                                    : QStringLiteral("已关闭");
        const QString bytes = info->bytesTotal > 0
            ? QStringLiteral("%1 / %2").arg(info->bytesRead).arg(info->bytesTotal)
            : QStringLiteral("%1（总量未知）").arg(info->bytesRead);
        QMessageBox::information(this, QStringLiteral("数据源信息"),
            QStringLiteral("名称：%1\n来源：%2\n类型：%3　状态：%4\n字节：%5\n解析器：%6　数据集：%7%8")
                .arg(info->name.isEmpty() ? QStringLiteral("(未命名)") : info->name,
                     info->origin, kindText, stateText, bytes)
                .arg(info->parsers.size())
                .arg(info->datasetIds.size())
                .arg(info->error.isEmpty() ? QString() : QStringLiteral("\n错误：%1").arg(info->error)));
        return;
    }
    if (chosen == remove || chosen == removeAll)
    {
        const bool all = chosen == removeAll;
        const QString name = info->name.isEmpty() ? QFileInfo(info->origin).fileName() : info->name;
        const QString ask = all ? QStringLiteral("确定移除全部 %1 个数据源及其全部通道？").arg(m_sources.ids().size())
                                : QStringLiteral("确定移除「%1」及其全部通道？").arg(name);
        if (QMessageBox::question(this, all ? QStringLiteral("移除全部数据源") : QStringLiteral("移除数据源"),
                ask, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QVector<int> targets = all ? m_sources.ids() : QVector<int>{sourceId};
        // intent 一次删一个：批量只是循环下发，顺序清完更安全（中途失败留已删的部分）。
        for (int id : targets)
        {
            Intent intent; intent.op = QStringLiteral("data.remove");
            intent.params[QStringLiteral("id")] = id;
            const auto rr = m_executor.execute(intent);
            if (!rr)
            {
                QMessageBox::warning(this, QStringLiteral("移除失败"), rr.error().message);
                break;
            }
        }
        return;
    }

    if (chosen == reload)
    {
        Intent intent; intent.op = QStringLiteral("data.reload");
        intent.params[QStringLiteral("id")] = sourceId;
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("重载失败"), r.error().message);
    }
    else if (chosen == exportCsv)
    {
        if (info->datasetIds.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("导出 CSV"), QStringLiteral("该数据源还没有解析出数据集"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV"), QString(), QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty()) return;
        Intent intent; intent.op = QStringLiteral("data.export");
        intent.params[QStringLiteral("dataset")] = info->datasetIds.first();
        intent.params[QStringLiteral("path")] = path;
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("导出失败"), r.error().message);
    }
    else if (chosen == clearCache)
    {
        Intent intent; intent.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("清除缓存失败"), r.error().message);
    }
    else if (chosen == activate)
        m_appCtx.setActiveSourceId(sourceId);
}

void DataManagerPanel::showColumnMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const QVariant dsId = item->data(0, kRoleDatasetId);
    const QVariant table = item->data(0, kRoleTable);
    const QVariant column = item->data(0, kRoleColumn);
    if (!dsId.isValid() || !table.isValid() || !column.isValid())
        return;
    QMenu menu(this);
    QAction *plot = menu.addAction(QStringLiteral("画到当前画布"));
    QAction *delDerived = menu.addAction(QStringLiteral("删除派生通道…"));
    menu.addSeparator();
    QMenu *derive = menu.addMenu(QStringLiteral("添加变换…"));
    QList<QAction *> deriveActs;
    for (const DeriveRecipe &r : kDeriveRecipes)
    {
        QAction *a = derive->addAction(QString::fromUtf8(r.label));
        a->setData(QString::fromUtf8(r.id));
        deriveActs << a;
    }
    derive->addSeparator();
    QAction *openEditor = derive->addAction(QStringLiteral("变换编辑器（算子/表达式/Lua）…"));
    openEditor->setToolTip(QStringLiteral("按能力过滤的变换选择器；支持单点/批量在线调试"));
    menu.addSeparator();
    QAction *openFilter = menu.addAction(QStringLiteral("添加过滤器…"));
    openFilter->setToolTip(QStringLiteral("有状态 Lua 过滤器（滑窗/微分/积分/缩放等），只读叠加到画布"));
    QAction *openState = menu.addAction(QStringLiteral("状态视图显示…"));
    openState->setToolTip(QStringLiteral("bool/枚举通道按状态着色画成顶部色带（对齐 PJ4 状态转换视图）"));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    const int datasetId = dsId.toInt();
    const QString tableName = table.toString();
    const QString columnName = column.toString();
    if (chosen == plot)
    {
        const int sourceId = m_sources.sourceOfDataset(datasetId);
        if (sourceId >= 0) m_appCtx.setActiveSourceId(sourceId);
        m_appCtx.setActiveChannel(datasetId, tableName, columnName);
        emit channelActivated(datasetId, tableName, columnName);
        return;
    }
    if (chosen == delDerived)
    {
        removeDerivedChannels(item);
        return;
    }
    if (chosen == openEditor)
    {
        // 统一变换编辑器：按 CapDerived 过滤，配置算子/表达式/Lua 后走 data.derive。
        const QJsonObject req = TransformEditorDialog::run(
            this, m_appCtx, CapDerived,
            QStringLiteral("数据源衍生 · %1.%2").arg(tableName, columnName));
        if (req.isEmpty())
            return;
        Intent intent;
        intent.op = QStringLiteral("data.derive");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        intent.params[QStringLiteral("transform")] = QVariant(req);
        const QString label = m_appCtx.transforms().get(
            req.value(QStringLiteral("descriptorId")).toString()).label;
        intent.params[QStringLiteral("name")] = tableName + QLatin1Char('.') + columnName
                                               + QStringLiteral(".") + label;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("变换失败"), r.error().message);
        else
        {
            const int count = r.value().payload.toMap().value(QStringLiteral("count")).toInt();
            QMessageBox::information(this, QStringLiteral("衍生完成"),
                QStringLiteral("变换「%1」已算出 %2 点并叠加到画布，原数据未改动。")
                    .arg(label).arg(count));
        }
        return;
    }
    if (chosen == openFilter)
    {
        // 有状态过滤器编辑器：选过滤器 + 参数表单 → data.filter 意图（只读叠加到画布）。
        const QJsonObject req = FilterEditorDialog::run(
            this, m_appCtx, QStringLiteral("%1.%2").arg(tableName, columnName));
        if (req.isEmpty())
            return;
        Intent intent;
        intent.op = QStringLiteral("data.filter");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        intent.params[QStringLiteral("filter")] = req.value(QStringLiteral("filter")).toString();
        intent.params[QStringLiteral("params")] = req.value(QStringLiteral("params")).toObject();
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("过滤失败"), r.error().message);
        else
        {
            const int count = r.value().payload.toMap().value(QStringLiteral("count")).toInt();
            QMessageBox::information(this, QStringLiteral("过滤完成"),
                QStringLiteral("过滤器「%1」已算出 %2 点并叠加到画布，原数据未改动。")
                    .arg(intent.params.value(QStringLiteral("filter")).toString()).arg(count));
        }
        return;
    }
    if (chosen == openState)
    {
        // 状态视图：整条通道游程编码成状态段 → data.state 意图 → 画布顶部色带。
        Intent intent;
        intent.op = QStringLiteral("data.state");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("状态视图失败"), r.error().message);
        return;
    }
    for (QAction *a : deriveActs)
    {
        if (chosen != a) continue;
        Intent intent; intent.op = QStringLiteral("data.derive");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        intent.params[QStringLiteral("filter")] = a->data().toString();
        intent.params[QStringLiteral("params")] = QString();
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("派生失败"), r.error().message);
        else
        {
            const QVariantMap pm = r.value().payload.toMap();
            const QString name = pm.value(QStringLiteral("name")).toString();
            const int count = pm.value(QStringLiteral("count")).toInt();
            const bool truncated = pm.value(QStringLiteral("truncated")).toBool();
            QMessageBox::information(this, QStringLiteral("派生完成"),
                QStringLiteral("已将「%1」送到画布叠加显示（%2 点%3）。\n原始数据未改动。")
                    .arg(name).arg(count)
                    .arg(truncated ? QStringLiteral("，已截取前 %1 点").arg(count) : QString()));
        }
        break;
    }
}

void DataManagerPanel::deriveColumn(QTreeWidgetItem *item, const QPoint &globalPos)
{
    if (!item)
        return;
    const int datasetId = item->data(0, kRoleDatasetId).toInt();
    const QString tableName = item->data(0, kRoleTable).toString();
    const QString columnName = item->data(0, kRoleColumn).toString();
    if (item->data(0, kRoleDatasetId).isValid() && !tableName.isEmpty() && !columnName.isEmpty())
    {
        // 与列右键「添加变换…」同一语义：弹出派生配方供选择，走 data.derive。
        QMenu menu(this);
        QList<QAction *> acts;
        for (const DeriveRecipe &r : kDeriveRecipes)
        {
            QAction *a = menu.addAction(QString::fromUtf8(r.label));
            a->setData(QString::fromUtf8(r.id));
            acts << a;
        }
        if (QAction *chosen = menu.exec(globalPos))
        {
            Intent intent; intent.op = QStringLiteral("data.derive");
            intent.params[QStringLiteral("dataset")] = datasetId;
            intent.params[QStringLiteral("table")] = tableName;
            intent.params[QStringLiteral("column")] = columnName;
            intent.params[QStringLiteral("filter")] = chosen->data().toString();
            const auto r = m_executor.execute(intent);
            if (!r)
                QMessageBox::warning(this, QStringLiteral("派生失败"), r.error().message);
        }
    }
}

void DataManagerPanel::removeDerivedChannels(QTreeWidgetItem *item)
{
    // 老界面删的是数据集里的派生列；NG 架构派生为只读叠加、不写回数据集，
    // 故这里删除画布上由该列派生出的曲线（真实动作，非死按钮）。
    const QString tableName = item ? item->data(0, kRoleTable).toString() : QString();
    const QString columnName = item ? item->data(0, kRoleColumn).toString() : QString();
    QVariantMap params;
    params[QStringLiteral("op")] = QStringLiteral("list");
    Result<QVariant> res{QVariant()};
    if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.series.list"), params, res)
        || !res)
    {
        QMessageBox::information(this, QStringLiteral("删除派生通道"),
            QStringLiteral("当前没有可删除的派生曲线。"));
        return;
    }
    const QString prefix = tableName + QLatin1Char('.') + columnName;
    QStringList keys;
    for (const QVariant &v : res.value().toList())
    {
        const QVariantMap m = v.toMap();
        const QString key = m.value(QStringLiteral("key")).toString();
        const QString col = m.value(QStringLiteral("column")).toString();
        // 派生曲线的列名带 .派生算子 后缀（如 att_roll.abs），天然含原列名前缀。
        if (!key.isEmpty() && col.contains(prefix) && col != columnName)
            keys << key;
    }
    if (keys.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("删除派生通道"),
            QStringLiteral("「%1」当前没有派生曲线。\n（NG 派生为只读叠加，不写回数据集）").arg(prefix));
        return;
    }
    int removed = 0;
    for (const QString &key : keys)
    {
        QVariantMap p; p[QStringLiteral("key")] = key;
        Result<QVariant> rr{QVariant()};
        if (m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.series.remove"), p, rr))
            ++removed;
    }
    QMessageBox::information(this, QStringLiteral("删除派生通道"),
        QStringLiteral("已移除 %1 条派生曲线。").arg(removed));
}

void DataManagerPanel::showRecentFilesMenu(const QPoint &globalPos)
{
    Intent intent; intent.op = QStringLiteral("data.recent");
    const auto r = m_executor.execute(intent);
    const QStringList files = r ? r.value().payload.toStringList() : QStringList();
    if (files.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("最近文件"), QStringLiteral("暂无最近文件"));
        return;
    }
    QMenu menu(this);
    for (const QString &path : files)
    {
        QAction *a = menu.addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        a->setData(path);
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    Intent load; load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = chosen->data().toString();
    const auto lr = m_executor.execute(load);
    if (!lr) QMessageBox::warning(this, QStringLiteral("打开失败"), lr.error().message);
}

void DataManagerPanel::openLogFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开飞行日志"), QString(),
        QStringLiteral("数据文件 (*.csv *.ulg *.log *.bin *.mcap *.tlog);;全部 (*)"));
    if (path.isEmpty()) return;
    Intent load; load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = path;
    const auto r = m_executor.execute(load);
    if (!r) QMessageBox::warning(this, QStringLiteral("打开失败"), r.error().message);
}

void DataManagerPanel::refresh()
{
    // 记住当前展开的源，重建后恢复。
    if (m_shownSourceId < 0 && m_sources.count() > 0)
        m_shownSourceId = m_sources.ids().first();
    rebuildTree();
}

int DataManagerPanel::sourceNodeCount() const { return m_tree->topLevelItemCount(); }

int DataManagerPanel::datasetNodeCount() const
{
    // dataset 层已在 UI 透明化，改从数据模型统计当前展开源的数据集数。
    const DataSourceInfo *info = m_sources.get(m_shownSourceId);
    return info ? info->datasetIds.size() : 0;
}

int DataManagerPanel::tableNodeCount(int datasetIndex) const
{
    // 单表时表节点被省掉（源 → 列），故从数据模型统计字段表数而非 UI 节点。
    // datasetIndex 保留以兼容调用方，跨数据集的表统一计入。
    Q_UNUSED(datasetIndex);
    const DataSourceInfo *info = m_sources.get(m_shownSourceId);
    if (!info) return 0;
    int n = 0;
    for (int dsId : info->datasetIds)
        if (const Dataset *ds = m_repo.get(dsId))
            for (int t = 0; t < ds->tableCount(); ++t)
                if (!isKeyValueTable(ds->table(t))) ++n;
    return n;
}

QString DataManagerPanel::sourceStateText(int row) const
{
    QTreeWidgetItem *src = m_tree->topLevelItem(row);
    return src ? src->data(0, Qt::UserRole).toString() : QString();
}

int DataManagerPanel::activeChip() const { return m_shownChip; }

QString DataManagerPanel::chipText(int index) const
{
    return (index >= 0 && index < 3 && m_chips[index]) ? m_chips[index]->text() : QString();
}

int DataManagerPanel::paramRowCount() const { return m_paramCount; }
int DataManagerPanel::msgRowCount() const { return m_msgCount; }

QWidget *createDatasetTreePanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    return new DataManagerPanel(appCtx, exec, parent);
}

// 视图层自注册：器官只声明类型字符串 "dataset-tree"，外壳据此装配本面板。
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)

} // namespace nextpilot::plotscope::gui
