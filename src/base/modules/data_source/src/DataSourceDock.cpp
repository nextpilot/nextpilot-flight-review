#include "DataSourceDock.h"

#include "SourceModel.h"
#include "host/extension/DataInputEntry.h"
#include "host/extension/DataInputRegistry.h"
#include "ps_kernel.h"
#include "host/services/live/LiveSession.h"
#include <QMap>
#include <QMessageBox>
#include <QTreeWidgetItemIterator>
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


// 源行名称按可用宽度省略中部，避免把右侧操作图标挤出去
class ElideLabel : public QLabel
{
public:
    ElideLabel(const QString &full, QWidget *parent) : QLabel(full, parent), m_full(full) {}
protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, width()));
    }
private:
    QString m_full;
};

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

QWidget *DataSourceDock::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    if (!m_srcRows)
        return nullptr;
    QWidget *rowsHost = m_srcRows->parentWidget();
    auto *row = new QWidget(rowsHost);
    row->setFixedHeight(40);
    row->setProperty("active", false);
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    row->setProperty("srcRow", true);
    row->setProperty("backed", backed);
    row->setProperty("srcPath", path);
    row->setProperty("srcMeta", meta);
    row->setProperty("srcName", name);
    row->setProperty("srcId", srcId);
    m_ctx.installRowEventFilter(row);
    row->setStyleSheet(QStringLiteral(
        "QWidget{background:transparent;border-left:2px solid transparent;border-bottom:1px solid #171f26;}"
        "QWidget[active=true]{background:#1a2127;border-left-color:#f2b134;}"
        "QWidget:hover{background:#1d242a;}"));
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(2, 2, 6, 2);
    lay->setSpacing(4);

    auto *expander = new QToolButton(row);
    expander->setObjectName(QStringLiteral("srcExpand"));
    expander->setText(QStringLiteral("▸"));
    expander->setToolTip(QStringLiteral("展开数据列"));
    expander->setCursor(Qt::PointingHandCursor);
    expander->setFixedSize(18, 26);
    expander->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#6e7d88;font-size:11px;padding:0;}"
        "QToolButton:hover{color:#f2b134;}"));
    lay->addWidget(expander);

    auto *dot = new QLabel(row);
    dot->setObjectName(QStringLiteral("srcDot"));
    dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    lay->addWidget(dot);

    auto *col = new QWidget(row);
    col->setAttribute(Qt::WA_TransparentForMouseEvents);
    col->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *colLay = new QVBoxLayout(col);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(0);
    auto *nameLb = new ElideLabel(name, col);
    nameLb->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:12px;background:transparent;"));
    auto *metaLb = new QLabel(meta, col);
    metaLb->setObjectName(QStringLiteral("srcMeta"));
    metaLb->setStyleSheet(QStringLiteral("color:#5d6c77;font-size:10px;background:transparent;"));
    colLay->addWidget(nameLb);
    colLay->addWidget(metaLb);
    lay->addWidget(col, 1);

    const ps::data_source::SourceData *rowSrc = m_ctx.sourceById(srcId);
    const bool isLive = rowSrc && rowSrc->live;

    // 实时源才给运行控制；文件源没有“暂停/继续”的概念。
    auto *pauseBtn = new QToolButton(row);
    pauseBtn->setObjectName(QStringLiteral("srcPause"));
    pauseBtn->setIcon(QIcon(rowSrc && rowSrc->paused ? QStringLiteral(":/icons/play.svg")
                                                      : QStringLiteral(":/icons/pause.svg")));
    pauseBtn->setIconSize(QSize(14, 14));
    pauseBtn->setToolTip(rowSrc && rowSrc->paused ? QStringLiteral("继续该实时源")
                                                   : QStringLiteral("暂停该实时源"));
    pauseBtn->setCursor(Qt::PointingHandCursor);
    pauseBtn->setFixedSize(20, 20);
    pauseBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:#222b33;border-radius:2px;}"));
    pauseBtn->setVisible(isLive);
    lay->addWidget(pauseBtn);
    connect(pauseBtn, &QToolButton::clicked, this, [this, srcId] {
        if (ps::data_source::SourceData *src = m_ctx.sourceById(srcId))
            m_ctx.setSourcePaused(src, !src->paused);
    });

    auto *dbg = new QToolButton(row);
    dbg->setIcon(QIcon(QStringLiteral(":/icons/script.svg")));
    dbg->setIconSize(QSize(14, 14));
    dbg->setToolTip(QStringLiteral("调试终端"));
    dbg->setCursor(Qt::PointingHandCursor);
    dbg->setFixedSize(22, 22);
    dbg->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:#222b33;border-radius:2px;}"));
    bool hasDebug = false;
    if (rowSrc)
    {
        const DataInputEntry *sp = DataInputRegistry::instance().findSourceById(rowSrc->sourceId);
        hasDebug = isLive && sp && sp->send;
    }
    dbg->setVisible(hasDebug);
    lay->addWidget(dbg);
    connect(dbg, &QToolButton::clicked, this, [this, srcId] {
        ps::data_source::SourceData *src = m_ctx.sourceById(srcId);
        if (src)
            m_ctx.openSourceDebug(src);
    });

    auto *cfg = new QToolButton(row);
    cfg->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    cfg->setIconSize(QSize(14, 14));
    cfg->setToolTip(QStringLiteral("配置数据源"));
    cfg->setCursor(Qt::PointingHandCursor);
    cfg->setFixedSize(22, 22);
    cfg->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:#222b33;border-radius:2px;}"));
    lay->addWidget(cfg);
    connect(cfg, &QToolButton::clicked, this, [this, srcId] {
        ps::data_source::SourceData *src = m_ctx.sourceById(srcId);
        if (src && src->live)
            m_ctx.configureLiveSource(src);
        else
            QMessageBox::information(this, QStringLiteral("配置"),
                QStringLiteral("文件源使用打开时的选项；实时源可重连修改参数。"));
    });

    auto *del = new QToolButton(row);
    del->setIcon(QIcon(QStringLiteral(":/icons/trash.svg")));
    del->setIconSize(QSize(14, 14));
    del->setToolTip(QStringLiteral("移除该数据源"));
    del->setCursor(Qt::PointingHandCursor);
    del->setFixedSize(20, 20);
    del->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#4a545d;padding:0;}"
        "QToolButton:hover{color:#f85149;background:#222b33;}"));
    lay->addWidget(del);
    connect(del, &QToolButton::clicked, this, [this, row] {
        m_ctx.removeSourceRow(row);
    });

    m_srcRows->addWidget(row);
    auto *panel = new ChannelTree(rowsHost);
    panel->setObjectName(QStringLiteral("srcPanel_%1").arg(srcId));
    panel->setProperty("srcPanel", true);
    panel->setHeaderHidden(true);
    panel->setRootIsDecorated(true);
    panel->setIndentation(14);
    panel->setDragEnabled(true);
    panel->setDragDropMode(QAbstractItemView::DragOnly);
    panel->setDefaultDropAction(Qt::CopyAction);
    panel->setFixedHeight(220);
    panel->setStyleSheet(QStringLiteral(
        "QTreeWidget{border:0;border-left:1px solid #232b33;background:#0b1014;"
        "font-size:11px;padding-left:6px;}"
        "QTreeWidget::item{height:18px;color:#9aa7b2;}"
        "QTreeWidget::item:hover{background:#151d24;color:#c9d1d9;}"
        "QTreeWidget::item:selected{background:#1c252d;color:#f2b134;}"));
    panel->hide();
    m_srcRows->addWidget(panel);
    const auto rebuildPanel = [this, panel, srcId] {
        panel->clear();
        ps::data_source::SourceData *sp = m_ctx.sourceById(srcId);
        if (!sp)
            return;
        // 参数是源的静态配置，放在最上面；数量少时直接展开。
        if (!sp->params.isEmpty())
        {
            auto *pg = new QTreeWidgetItem(panel, {QStringLiteral("参数 %1").arg(sp->params.size())});
            pg->setForeground(0, QColor(QStringLiteral("#6e7d88")));
            for (const auto &pr : sp->params)
            {
                auto *item = new QTreeWidgetItem(pg, {QStringLiteral("%1 = %2").arg(pr.first, pr.second)});
                item->setToolTip(0, QStringLiteral("%1 = %2").arg(pr.first, pr.second));
            }
            pg->setExpanded(sp->params.size() <= 8);
        }
        if (!sp->msgs.isEmpty())
        {
            const bool textLog = sp->msgs.first().value(1) == QStringLiteral("—");
            const QString groupTitle = textLog
                ? QStringLiteral("消息 %1").arg(sp->msgs.size())
                : QStringLiteral("话题 %1").arg(sp->msgs.size());
            auto *mg = new QTreeWidgetItem(panel, {groupTitle});
            mg->setForeground(0, QColor(QStringLiteral("#6e7d88")));
            for (const QStringList &m : sp->msgs)
            {
                bool ok = false;
                const double mt = m.value(3).toDouble(&ok);
                QString text;
                if (textLog)
                {
                    text = QStringLiteral("%1  ·  %2 s").arg(m.value(0), m.value(3));
                    ok = true;
                }
                else
                {
                    text = QStringLiteral("%1  ·  %2  ·  %3 字段")
                               .arg(m.value(0), m.value(1), m.value(2));
                    ok = false;
                }
                auto *item = new QTreeWidgetItem(mg, {text});
                item->setToolTip(0, text);
                if (ok)
                {
                    item->setData(0, Qt::UserRole + 3, mt);
                    item->setData(0, Qt::UserRole + 4, m.value(0));
                }
            }
            mg->setExpanded(sp->msgs.size() <= 6);
        }
        QMap<QString, QTreeWidgetItem *> groups;
        for (const PsChannelInfo *ch : sp->channels)
        {
            const QString full = QString::fromUtf8(ch->name);
            const int dot = full.lastIndexOf(QLatin1Char('.'));
            const QString group = dot > 0 ? full.left(dot) : QString();
            const QString leaf = dot > 0 ? full.mid(dot + 1) : full;
            QTreeWidgetItem *parent = nullptr;
            if (!group.isEmpty())
            {
                auto it = groups.find(group);
                if (it == groups.end())
                {
                    auto *g = new QTreeWidgetItem(panel, {group});
                    g->setForeground(0, QColor(QStringLiteral("#6e7d88")));
                    it = groups.insert(group, g);
                }
                parent = it.value();
            }
            auto *item = parent ? new QTreeWidgetItem(parent, {leaf})
                                : new QTreeWidgetItem(panel, {leaf});
            QString tip = full;
            if (ch->unit && *ch->unit)
                tip += QStringLiteral(" [%1]").arg(QString::fromUtf8(ch->unit));
            item->setToolTip(0, tip);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(
                                                          reinterpret_cast<qulonglong>(ch)));
            item->setData(0, Qt::UserRole + 1, full);
            item->setData(0, Qt::UserRole + 2, srcId);
        }
        for (auto *g : groups)
            g->setExpanded(groups.size() <= 10);
        if (groups.size() > 10 && !groups.isEmpty())
            groups.first()->setExpanded(true);
        int rows = 0;
        for (QTreeWidgetItemIterator it(panel); *it; ++it)
            ++rows;
        panel->setFixedHeight(qMin(420, qMax(160, rows * 18 + 6)));
    };
    connect(expander, &QToolButton::clicked, this, [this, row, panel, expander, rebuildPanel] {
        if (panel->isVisible())
        {
            panel->hide();
            expander->setText(QStringLiteral("▸"));
            return;
        }
        rebuildPanel();
        panel->show();
        expander->setText(QStringLiteral("▾"));
        m_ctx.activateSource(row);
        // 同一时间只展开一个源，数据列就紧贴在源下面。
        QWidget *host = m_srcRows ? m_srcRows->parentWidget() : nullptr;
        if (host)
            for (auto *other : host->findChildren<ChannelTree *>())
                if (other != panel && other->property("srcPanel").toBool())
                {
                    other->hide();
                    const int oi = m_srcRows->indexOf(other);
                    if (oi > 0)
                        if (QWidget *orow = m_srcRows->itemAt(oi - 1)->widget())
                            if (auto *ob = orow->findChild<QToolButton *>(QStringLiteral("srcExpand")))
                                ob->setText(QStringLiteral("▸"));
                }
    });
    panel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(panel, &QTreeWidget::customContextMenuRequested, this, [this, panel](const QPoint &pos) {
        auto *item = panel->itemAt(pos);
        if (!item)
            return;
        const QVariant mtv = item->data(0, Qt::UserRole + 3);
        if (!mtv.isValid())
            return;
        const double t = mtv.toDouble();
        const QString label = item->data(0, Qt::UserRole + 4).toString();
        QMenu menu(this);
        QAction *add = menu.addAction(
            QStringLiteral("在 %1 s 添加标签「%2」").arg(t, 0, 'f', 2).arg(label));
        if (menu.exec(panel->viewport()->mapToGlobal(pos)) == add)
            m_ctx.addTimeMarker(t, label, false);
    });
    connect(panel, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        m_ctx.addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
        m_ctx.log(QStringLiteral("双击加曲线：%1").arg(item->text(0)));
    });
    m_ctx.bindSourceRow(srcId, row);
    refreshSrcCount();
    if (backed)
        m_ctx.activateSource(row);
    return row;
}


void DataSourceDock::refreshSrcCount()
{
    if (!m_srcCount || !m_srcRows)
        return;
    int total = 0;
    int connected = 0;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!row || row->property("srcPanel").toBool())
            continue;
        ++total;
        if (row->property("connected").toBool())
            ++connected;
    }
    m_srcCount->setText(QStringLiteral("%1 源 · %2 已连接").arg(total).arg(connected));
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
    const bool empty = total == 0;
    if (m_srcListScroll && m_srcListToggle)
    {
        const bool show = !empty && m_srcListToggle->text() != QStringLiteral("▸");
        m_srcListScroll->setVisible(show);
        if (m_srcListScroll->parentWidget())
            m_srcListScroll->parentWidget()->setVisible(!empty);
    }
    if (m_srcStack && m_srcEmpty)
    {
        m_srcStack->setVisible(empty);
        if (empty)
            m_srcStack->setCurrentWidget(m_srcEmpty);
        else if (m_srcStack->currentWidget() == m_srcEmpty && m_srcTabs)
            m_srcStack->setCurrentIndex(m_srcTabs->currentIndex());
    }
    syncSrcChips();
    updateSrcStatus();
}

void DataSourceDock::updateSrcStatus()
{
    if (!m_srcStatus)
        return;
    const ps::data_source::SourceData *src = m_ctx.sourceById(m_ctx.activeSourceId());
    if (!src)
    {
        m_srcStatus->setText(QStringLiteral("—"));
        return;
    }
    const int channels = src->live
        ? (src->liveSession ? src->liveSession->channels().size() : 0)
        : src->channels.size();
    qint64 points = 0;
    double duration = 0.0;
    double rate = 0.0;
    if (src->live)
    {
        points = src->liveSession ? src->liveSession->totalSamples() : 0;
    }
    else
    {
        for (const PsChannelInfo *ch : src->channels)
            points += ch->point_count;
    }
    const QVector<const PsChannelInfo *> view =
        src->live ? (src->liveSession ? src->liveSession->channels() : QVector<const PsChannelInfo *>())
                  : src->channels;
    if (!view.isEmpty() && view.first()->point_count > 1)
    {
        const PsChannelInfo *ch = view.first();
        duration = ch->time[ch->point_count - 1] - ch->time[0];
        if (duration > 0.0)
            rate = (ch->point_count - 1) / duration;
    }
    QString rateText = src->live ? QStringLiteral("实时") : QStringLiteral("—");
    if (rate > 0.0)
        rateText = rate >= 1000.0 ? QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'f', 1)
                                  : QStringLiteral("%1 Hz").arg(rate, 0, 'f', rate >= 10.0 ? 0 : 1);
    m_srcStatus->setText(QStringLiteral("通道 %1 · %2 点 · 时长 %3 s · %4")
                             .arg(channels)
                             .arg(points)
                             .arg(duration, 0, 'f', 2)
                             .arg(rateText));
    m_srcStatus->setToolTip(m_srcStatus->text());
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
