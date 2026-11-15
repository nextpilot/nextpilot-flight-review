#include "DataSourceDock.h"

#include "SourceModel.h"
#include "host/extension/DataInputEntry.h"
#include "host/extension/DataInputRegistry.h"
#include "ps_kernel.h"
#include <QAbstractItemView>

#include <QButtonGroup>
#include <QDrag>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace
{

void compactValueTable(QTableWidget *t)
{
    if (!t)
        return;
    t->setAlternatingRowColors(false);
    t->setWordWrap(false);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(21);
    t->verticalHeader()->setMinimumSectionSize(21);
    t->horizontalHeader()->setFixedHeight(23);
    QFont f = t->font();
    f.setPixelSize(12);
    t->setFont(f);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
}

// 字段树拖拽：Item 里存的是内核通道指针，默认 QTreeWidget 的 MIME 带不过去，自己塞一条
class ChannelTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-plotscope-channel"), QByteArray::number(ptr));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

// 数据源 Dock 接收文件拖拽
class FileDropHost : public QWidget
{
public:
    std::function<void(const QStringList &)> onFiles;

    explicit FileDropHost(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAcceptDrops(true);
    }

protected:
    static QStringList localFiles(const QMimeData *mime)
    {
        QStringList files;
        if (!mime || !mime->hasUrls())
            return files;
        for (const QUrl &url : mime->urls())
            if (url.isLocalFile())
                files << url.toLocalFile();
        return files;
    }

    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (!localFiles(e->mimeData()).isEmpty())
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (!localFiles(e->mimeData()).isEmpty())
            e->acceptProposedAction();
    }

    void dropEvent(QDropEvent *e) override
    {
        const QStringList files = localFiles(e->mimeData());
        if (files.isEmpty())
            return;
        e->acceptProposedAction();
        if (onFiles)
            onFiles(files);
    }
};

} // namespace

namespace ps::data_source
{

DataSourceDock::DataSourceDock(Context context, QWidget *parent)
    : QWidget(parent), m_ctx(std::move(context))
{
    auto *srcHost = new FileDropHost(this);
    srcHost->onFiles = [this](const QStringList &files) {
        for (const QString &f : files)
            m_ctx.loadFiles(QStringList{f});
    };
    auto *srcLay = new QVBoxLayout(srcHost);
    srcLay->setContentsMargins(0, 2, 0, 0);
    srcLay->setSpacing(0);

    // 上段：源列表（可折叠，最高 124px），每条=状态点 + 名称 + 速率/条目 + 配置
    auto *srcListWrap = new QWidget(srcHost);
    auto *listLay = new QVBoxLayout(srcListWrap);
    listLay->setContentsMargins(0, 0, 0, 0);
    listLay->setSpacing(0);
    auto *listHead = new QWidget(srcListWrap);
    listHead->setFixedHeight(22);
    listHead->setStyleSheet(QStringLiteral("background:#11171c;border-bottom:1px solid #232b33;"));
    auto *headLay = new QHBoxLayout(listHead);
    headLay->setContentsMargins(8, 0, 4, 0);
    headLay->setSpacing(4);
    auto *headCap = new QLabel(QStringLiteral("已加载源"), listHead);
    headCap->setStyleSheet(QStringLiteral("color:#6e7d88;font-size:10px;background:transparent;"));
    headLay->addWidget(headCap);
    headLay->addStretch();
    m_srcListToggle = new QToolButton(listHead);
    m_srcListToggle->setText(QStringLiteral("▾"));
    m_srcListToggle->setToolTip(QStringLiteral("收起 / 展开源列表"));
    m_srcListToggle->setCursor(Qt::PointingHandCursor);
    m_srcListToggle->setFixedSize(18, 18);
    m_srcListToggle->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#6e7d88;font-size:11px;padding:0;}"
        "QToolButton:hover{color:#c9d1d9;}"));
    headLay->addWidget(m_srcListToggle);
    listLay->addWidget(listHead);

    auto *rowsHost = new QWidget(srcListWrap);
    m_srcRows = new QVBoxLayout(rowsHost);
    m_srcRows->setContentsMargins(0, 0, 0, 0);
    m_srcRows->setSpacing(0);
    m_srcRows->setAlignment(Qt::AlignTop);
    m_srcListScroll = new QScrollArea(srcListWrap);
    m_srcListScroll->setWidget(rowsHost);
    m_srcListScroll->setWidgetResizable(true);
    m_srcListScroll->setFrameShape(QFrame::NoFrame);
    m_srcListScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_srcListScroll->setMaximumHeight(16777215);
    m_srcListScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:0;}"));
    m_srcListScroll->setAlignment(Qt::AlignTop);
    listLay->addWidget(m_srcListScroll);
    srcLay->addWidget(srcListWrap, 1);
    connect(m_srcListToggle, &QToolButton::clicked, this, [this] {
        const bool hide = m_srcListScroll->isVisible();
        m_srcListScroll->setVisible(!hide);
        m_srcListToggle->setText(hide ? QStringLiteral("▸") : QStringLiteral("▾"));
    });

    // 中段：字段/参数/消息 过滤 chip（QTabBar 保留为状态模型，不显示）
    m_srcTabs = new QTabBar(srcHost);
    m_srcTabs->addTab(QStringLiteral("字段 0"));
    m_srcTabs->addTab(QStringLiteral("参数 0"));
    m_srcTabs->addTab(QStringLiteral("消息 0"));
    m_srcTabs->hide();
    auto *chipHost = new QWidget(srcHost);
    chipHost->setStyleSheet(QStringLiteral("background:#0f151a;border-bottom:1px solid #232b33;"));
    auto *chipLay = new QHBoxLayout(chipHost);
    chipLay->setContentsMargins(8, 5, 8, 5);
    chipLay->setSpacing(4);
    auto *chipGroup = new QButtonGroup(chipHost);
    chipGroup->setExclusive(true);
    for (int i = 0; i < 3; ++i)
    {
        auto *chip = new QToolButton(chipHost);
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        chip->setText(m_srcTabs->tabText(i));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chip->setFixedHeight(22);
        chip->setStyleSheet(QStringLiteral(
            "QToolButton{background:#161d23;border:1px solid #2a343d;border-radius:9px;"
            "color:#8494a0;font-size:11px;padding:0 7px;}"
            "QToolButton:hover{color:#c9d1d9;border-color:#3d4a55;}"
            "QToolButton:checked{background:#2a2416;border-color:#6b5320;color:#f2b134;}"
            "QToolButton:disabled{color:#4a545e;border-color:#232b33;background:transparent;}"
            "QToolButton:disabled:checked{color:#5d6c77;border-color:#2a343d;background:#161d23;}"));
        chipGroup->addButton(chip, i);
        chipLay->addWidget(chip, 1);
        m_srcChips.append(chip);
        connect(chip, &QToolButton::clicked, this, [this, i] { m_srcTabs->setCurrentIndex(i); });
    }
    chipHost->hide();
    srcLay->addWidget(chipHost);
    connect(m_srcTabs, &QTabBar::currentChanged, this, [this] {
        applySrcFilter();
        syncSrcChips();
    });

    // 搜索：字段树/参数表/消息表共用，按当前 chip 生效
    m_srcFilter = new QLineEdit(srcHost);
    m_srcFilter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_srcFilter->setClearButtonEnabled(true);
    m_srcFilter->setFixedHeight(26);
    m_srcFilter->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0f151a;border:0;border-bottom:1px solid #232b33;"
        "color:#c9d1d9;font-size:11px;padding:0 8px;}"
        "QLineEdit:focus{border-bottom:1px solid #f2b134;}"));
    srcLay->addWidget(m_srcFilter);
    connect(m_srcFilter, &QLineEdit::textChanged, this, [this] { applySrcFilter(); });

    m_srcStack = new QStackedWidget(srcHost);

    m_srcTree = new ChannelTree(m_srcStack);
    m_srcTree->setHeaderHidden(true);
    m_srcTree->setDragEnabled(true);
    m_srcTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_srcTree->setDefaultDropAction(Qt::CopyAction);
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        m_ctx.addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
        m_ctx.log(QStringLiteral("双击加曲线：%1").arg(item->text(0)));
    });
    m_srcTree->setRootIsDecorated(true);
    m_srcTree->setStyleSheet(QStringLiteral("QTreeWidget{border:0;background:#0d1117;font-size:12px;}"));
    m_srcTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_srcTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_srcTree->itemAt(pos);
        if (!item || item->childCount() > 0)
            return;
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        if (name.isEmpty())
            return;
        ps::data_source::SourceData *src = m_ctx.sourceById(m_ctx.activeSourceId());
        if (!src || !src->ds)
            return;
        QMenu menu(this);
        menu.setAttribute(Qt::WA_StyledBackground, true);
        menu.setAttribute(Qt::WA_TranslucentBackground, false);
        menu.setAutoFillBackground(true);
        QPalette pal = menu.palette();
        pal.setColor(QPalette::Window, QColor(QStringLiteral("#161b20")));
        pal.setColor(QPalette::Base, QColor(QStringLiteral("#161b20")));
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#c9d1d9")));
        pal.setColor(QPalette::Text, QColor(QStringLiteral("#c9d1d9")));
        menu.setPalette(pal);
        menu.setStyleSheet(QStringLiteral(
            "QMenu{background:#161b20;color:#c9d1d9;border:1px solid #2d3841;padding:4px;}"
            "QMenu::item{padding:5px 24px 5px 20px;font-size:12px;color:#c9d1d9;background:#161b20;}"
            "QMenu::item:selected{background:#1f2a33;color:#f2b134;}"));
        QAction *add = menu.addAction(QStringLiteral("添加变换…"));
        QAction *draw = menu.addAction(QStringLiteral("画到当前画布"));
        QAction *del = src->derived.contains(name) ? menu.addAction(QStringLiteral("删除派生通道")) : nullptr;
        QAction *chosen = menu.exec(m_srcTree->viewport()->mapToGlobal(pos));
        if (chosen == draw)
        {
            const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
            if (ptr)
                m_ctx.addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
            return;
        }
        if (chosen == del && del)
        {
            if (ps_dataset_remove_channel(src->ds, name.toUtf8().constData()) == 0)
            {
                for (int i = src->recipes.size() - 1; i >= 0; --i)
                {
                    const ps::data_source::TransformRecipe &r = src->recipes[i];
                    const bool match = r.kind == QLatin1String("quat")
                                           ? name.startsWith(r.output + QLatin1Char('.'))
                                           : r.output == name;
                    if (match)
                        src->recipes.removeAt(i);
                }
                src->derived.removeAll(name);
                m_ctx.removeChannel(name);
                m_ctx.refreshActiveSource();
                m_ctx.log(QStringLiteral("删除派生通道：%1").arg(name));
            }
            return;
        }
        if (chosen != add)
            return;
        m_ctx.addTransformFor(name);
    });
    auto *transformKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    connect(transformKey, &QShortcut::activated, this, [this] {
        auto *item = m_srcTree ? m_srcTree->currentItem() : nullptr;
        if (!item || item->childCount() > 0)
            return;
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        if (!name.isEmpty())
            m_ctx.addTransformFor(name);
    });
    m_srcStack->addWidget(m_srcTree);

    m_paramTable = new QTableWidget(0, 2, m_srcStack);
    m_paramTable->setHorizontalHeaderLabels({QStringLiteral("参数"), QStringLiteral("值")});
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->horizontalHeader()->setStretchLastSection(false);
    m_paramTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_paramTable->setColumnWidth(1, 110);
    m_paramTable->setShowGrid(false);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    compactValueTable(m_paramTable);
    m_srcStack->addWidget(m_paramTable);

    m_msgTable = new QTableWidget(0, 4, m_srcStack);
    m_msgTable->setHorizontalHeaderLabels(
        {QStringLiteral("消息"), QStringLiteral("速率"), QStringLiteral("字段"), QStringLiteral("首时刻")});
    m_msgTable->verticalHeader()->setVisible(false);
    m_msgTable->horizontalHeader()->setStretchLastSection(false);
    m_msgTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_msgTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_msgTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_msgTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_msgTable->setColumnWidth(1, 58);
    m_msgTable->setColumnWidth(2, 36);
    m_msgTable->setColumnWidth(3, 64);
    m_msgTable->setShowGrid(false);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_msgTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_msgTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_msgTable->rowAt(pos.y());
        if (row < 0)
            return;
        QTableWidgetItem *topicItem = m_msgTable->item(row, 0);
        QTableWidgetItem *timeItem = m_msgTable->item(row, 3);
        if (!topicItem || !timeItem || timeItem->text().isEmpty())
            return;
        bool ok = false;
        const double t = timeItem->text().toDouble(&ok);
        if (!ok)
            return;
        QMenu menu(this);
        QAction *add = menu.addAction(
            QStringLiteral("在 %1 s 添加标签「%2」").arg(t, 0, 'f', 2).arg(topicItem->text()));
        if (menu.exec(m_msgTable->viewport()->mapToGlobal(pos)) == add)
            m_ctx.addTimeMarker(t, topicItem->text(), false);
    });
    compactValueTable(m_msgTable);
    m_msgTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_msgTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_msgTable->rowAt(pos.y());
        if (row < 0 || row >= m_ctx.sourceMsgs().size())
            return;
        ps::data_source::SourceData *src = m_ctx.sourceById(m_ctx.activeSourceId());
        const double raw = m_ctx.sourceMsgs().at(row).value(3).toDouble();
        const double t = raw - (src ? src->offsetSec : 0.0);
        const QString topic = m_ctx.sourceMsgs().at(row).value(0);
        QMenu menu(this);
        QAction *add = menu.addAction(
            QStringLiteral("在 %1 s 添加标签「%2」").arg(t, 0, 'f', 3).arg(topic));
        QAction *chosen = menu.exec(m_msgTable->viewport()->mapToGlobal(pos));
        if (chosen == add)
            m_ctx.addTimeMarker(t, topic, false);
    });
    m_srcStack->addWidget(m_msgTable);

    // 空态：没有再摆三个 0，直接给打开日志 / 连接实时源 / 最近文件
    m_srcEmpty = new QWidget(m_srcStack);
    auto *emptyLay = new QVBoxLayout(m_srcEmpty);
    emptyLay->setContentsMargins(14, 16, 14, 16);
    emptyLay->setSpacing(6);
    auto *emptyTitle = new QLabel(QStringLiteral("还没有数据源"), m_srcEmpty);
    emptyTitle->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:13px;font-weight:600;background:transparent;"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyHint = new QLabel(QStringLiteral("打开飞行日志，或连接一个实时源开始分析"), m_srcEmpty);
    emptyHint->setStyleSheet(QStringLiteral("color:#6e7d88;font-size:10px;background:transparent;"));
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyLay->addWidget(emptyTitle);
    emptyLay->addWidget(emptyHint);
    emptyLay->addSpacing(6);
    auto mkEmptyBtn = [&](const QString &text) {
        auto *b = new QPushButton(text, m_srcEmpty);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(28);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;"
            "color:#c9d1d9;font-size:11px;text-align:center;}"
            "QPushButton:hover{background:#222b33;border-color:#f2b134;color:#f2b134;}"));
        emptyLay->addWidget(b);
        return b;
    };
    auto *openBtn = mkEmptyBtn(QStringLiteral("打开日志"));
    auto *liveBtn = mkEmptyBtn(QStringLiteral("连接实时源"));
    auto *recentBtn = mkEmptyBtn(QStringLiteral("最近文件"));
    emptyLay->addStretch();
    connect(openBtn, &QPushButton::clicked, this, [this] {
        QStringList filters;
        for (const DataInputEntry &lp : DataInputRegistry::instance().fileEntries())
            if (!lp.filter.isEmpty())
                filters << lp.filter;
        filters << QStringLiteral("全部 (*)");
        const QStringList fs = QFileDialog::getOpenFileNames(
            this, QStringLiteral("打开飞行日志"), {}, filters.join(QStringLiteral(";;")));
        for (const QString &f : fs)
            m_ctx.loadFiles(QStringList{f});
    });
    connect(liveBtn, &QPushButton::clicked, this, [this, liveBtn] {
        QMenu menu(this);
        for (const DataInputEntry &sp : DataInputRegistry::instance().streamEntries())
        {
            QAction *a = menu.addAction(sp.label);
            connect(a, &QAction::triggered, this, [this, id = sp.id] { m_ctx.connectLiveSource(id); });
        }
        if (menu.isEmpty())
            menu.addAction(QStringLiteral("没有可用的实时源扩展"))->setEnabled(false);
        menu.exec(liveBtn->mapToGlobal(QPoint(0, liveBtn->height())));
    });
    connect(recentBtn, &QPushButton::clicked, this, [this, recentBtn] {
        QMenu menu(this);
        QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
        const QStringList recent = st.value(QStringLiteral("recentFiles")).toStringList();
        for (const QString &path : recent)
        {
            QAction *a = menu.addAction(QFileInfo(path).fileName());
            a->setToolTip(path);
            connect(a, &QAction::triggered, this, [this, path] { m_ctx.loadFiles(QStringList{path}); });
        }
        if (menu.isEmpty())
            menu.addAction(QStringLiteral("暂无最近文件"))->setEnabled(false);
        menu.exec(recentBtn->mapToGlobal(QPoint(0, recentBtn->height())));
    });
    m_srcStack->addWidget(m_srcEmpty);
    m_srcStack->setCurrentWidget(m_srcEmpty);

    connect(m_srcTabs, &QTabBar::currentChanged, m_srcStack, &QStackedWidget::setCurrentIndex);
    srcLay->addWidget(m_srcStack, 1);
    m_srcStack->hide();
    srcLay->removeWidget(m_srcFilter);
    srcLay->insertWidget(0, m_srcFilter);

    // 底部状态条：通道 / 时长 / 速率
    m_srcStatus = new QLabel(QStringLiteral("—"), srcHost);
    m_srcStatus->setFixedHeight(20);
    m_srcStatus->setStyleSheet(QStringLiteral(
        "background:#0b1015;border-top:1px solid #232b33;color:#6e7d88;font-size:10px;padding:0 8px;"));
    srcLay->addWidget(m_srcStatus);

    // 双击通道 → 画到当前页第一格（后续换成字段拖拽，见 §8.10）
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const qulonglong raw = item->data(0, Qt::UserRole).toULongLong();
        const PsChannelInfo *ch = reinterpret_cast<const PsChannelInfo *>(raw);
        if (!ch)
            return;
        m_ctx.setChannel(ch);
        m_ctx.log(QStringLiteral("画布加载通道：%1").arg(QString::fromUtf8(ch->name)));
    });

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(srcHost);
}

void DataSourceDock::syncSrcChips()
{
    if (!m_srcTabs || m_srcChips.isEmpty())
        return;
    const bool enabled = m_ctx.hasSources ? m_ctx.hasSources() : true;
    for (int i = 0; i < m_srcChips.size() && i < m_srcTabs->count(); ++i)
    {
        auto *chip = m_srcChips[i];
        chip->setText(m_srcTabs->tabText(i));
        chip->setChecked(m_srcTabs->currentIndex() == i);
        chip->setEnabled(enabled);
    }
}

void DataSourceDock::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    if (QWidget *host = m_srcRows ? m_srcRows->parentWidget() : nullptr)
        for (auto *panel : host->findChildren<ChannelTree *>())
        {
            if (!panel->property("srcPanel").toBool())
                continue;
            for (int i = 0; i < panel->topLevelItemCount(); ++i)
            {
                QTreeWidgetItem *g = panel->topLevelItem(i);
                const bool groupHit = q.isEmpty() || g->text(0).contains(q, Qt::CaseInsensitive);
                int hits = 0;
                for (int j = 0; j < g->childCount(); ++j)
                {
                    QTreeWidgetItem *leaf = g->child(j);
                    const bool hit = groupHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                    leaf->setHidden(!hit);
                    if (hit)
                        ++hits;
                }
                g->setHidden(q.isEmpty() ? false : (!groupHit && hits == 0));
                if (!q.isEmpty() && hits > 0)
                    g->setExpanded(true);
            }
        }
    if (tab == 0 && m_srcTree)
    {
        for (int i = 0; i < m_srcTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *topic = m_srcTree->topLevelItem(i);
            const bool topicHit = q.isEmpty() || topic->text(0).contains(q, Qt::CaseInsensitive);
            int childHit = 0;
            for (int j = 0; j < topic->childCount(); ++j)
            {
                QTreeWidgetItem *leaf = topic->child(j);
                const bool hit = topicHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                if (hit)
                    ++childHit;
            }
            topic->setHidden(q.isEmpty() ? false : (!topicHit && childHit == 0));
            if (!q.isEmpty() && childHit > 0)
                topic->setExpanded(true);
        }
    }
    else if (tab == 1 && m_paramTable)
    {
        for (int r = 0; r < m_paramTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_paramTable->columnCount() && !hit; ++c)
                hit = m_paramTable->item(r, c) && m_paramTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_paramTable->setRowHidden(r, !hit);
        }
    }
    else if (tab == 2 && m_msgTable)
    {
        for (int r = 0; r < m_msgTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_msgTable->columnCount() && !hit; ++c)
                hit = m_msgTable->item(r, c) && m_msgTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_msgTable->setRowHidden(r, !hit);
        }
    }
}

} // namespace ps::data_source
