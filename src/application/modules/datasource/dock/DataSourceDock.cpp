#include "DataSourceDock.h"

#include "SourceModel.h"
#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "ps_kernel.h"
#include "LiveSession.h"
#include "DatasetCache.h"
#include "UiDialogEngine.h"
#include "DataSourceDialog.h"
#include "record/StreamRecorder.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QtEndian>
#include <QMap>
#include <QMessageBox>
#include <QInputDialog>
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
        for (const DataSourceEntry &lp : DataSourceRegistry::instance().fileEntries())
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
        for (const DataSourceEntry &sp : DataSourceRegistry::instance().streamEntries())
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

namespace {
struct FanoutParser
{
    QString id;
    void *handle = nullptr;
    LiveSession *session = nullptr;
    QString prefix;
    PsSourceSink sink{};
};

int32_t fanoutChannel(void *ctx, const char *name, const char *unit,
                      const double *time_s, const double *value, uint32_t count)
{
    auto *ep = static_cast<FanoutParser *>(ctx);
    if (!ep || !ep->session || !name)
        return 1;
    const QByteArray full = ep->prefix.toUtf8() + '.' + QByteArray(name);
    return ep->session->sink()->channel_batch(ep->session, full.constData(), unit,
                                              time_s, value, count);
}

int32_t fanoutEvent(void *ctx, const char *topic, const char *payload)
{
    auto *ep = static_cast<FanoutParser *>(ctx);
    if (!ep || !ep->session)
        return 1;
    return ep->session->sink()->event(ep->session, topic, payload);
}

QVector<QStringList> ulogLogMessages(const QString &path, double base)
{
    QVector<QStringList> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray buf = f.readAll();
    if (buf.size() < 24 || std::memcmp(buf.constData(), "ULog", 4) != 0) return out;
    int pos = 16;
    while (pos + 3 <= buf.size())
    {
        const quint16 size = qFromLittleEndian<quint16>(buf.constData() + pos);
        const char ty = buf.at(pos + 2);
        const int body = pos + 3;
        if (body + size > buf.size()) break;
        const char *p = buf.constData() + body;
        if (ty == 'L' && size >= 9)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 1);
            QByteArray text(p + 9, size - 9);
            const int nul = text.indexOf('\0');
            if (nul >= 0) text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        else if (ty == 'C' && size >= 11)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 3);
            QByteArray text(p + 11, size - 11);
            const int nul = text.indexOf('\0');
            if (nul >= 0) text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        pos = body + size;
    }
    return out;
}
} // namespace

void DataSourceDock::updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor)
{
    if (!row)
        return;
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    if (auto *label = row->findChild<QLabel *>(QStringLiteral("srcMeta")))
        label->setText(meta);
    if (auto *dot = row->findChild<QLabel *>(QStringLiteral("srcDot")))
        dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    refreshSrcCount();
}

bool DataSourceDock::loadFile(const QString &path, bool interactive)
{
    char err[256] = {0};
    PsDataset *ds = nullptr;
    const QFileInfo fi(path);
    // 加载器由 registry 提供：app 不再按扩展名写 if/else，也不再为每个格式写对话框
    const DataSourceEntry *entry = DataSourceRegistry::instance().findFileBySuffix(fi.suffix());
    if (!entry || !entry->load)
    {
        QMessageBox::warning(this, QStringLiteral("加载失败"),
                             QStringLiteral("没有匹配的加载器：%1").arg(fi.fileName()));
        return false;
    }
    const bool is_tree = entry->tree;
    QString opts;
    if (!entry->uiDialog.isEmpty())
    {
        // 扩展自带 .ui：通用引擎加载，app 不认识任何控件
        if (interactive)
        {
            UiDialogEngine dlg(UiDialogSpec{entry->label, entry->uiDialog, entry->uiData}, path, this);
            if (dlg.exec() != QDialog::Accepted)
                return false;
            opts = dlg.optionsJson();
        }
        else if (entry->defaultOptions)
            opts = entry->defaultOptions(path);
    }
    else if (!entry->schema.isEmpty())
    {
        // 交互打开：schema 渲染选项；恢复布局：按加载器默认探测静默加载
        if (interactive)
        {
            DataSourceDialog dlg(*entry, path, this);
            if (dlg.exec() != QDialog::Accepted)
                return false;
            opts = dlg.optionsJson();
        }
        else if (entry->defaultOptions)
            opts = entry->defaultOptions(path);
    }
    // 解析缓存：同文件 + 同 loader + 同选项二次打开直接还原，不再 probe/parse。
    if (entry->cacheable)
        ds = DatasetCache::load(path, entry->cacheNamespace, opts);
    if (ds)
    {
        m_ctx.log(QStringLiteral("解析缓存命中：%1").arg(fi.fileName()));
    }
    else
    {
        const int32_t rc = entry->load(path, opts, &ds, err, sizeof(err));
        if (rc != 0)
        {
            const QString msg = QString::fromUtf8(err);
            m_ctx.log(QStringLiteral("加载失败：%1").arg(msg));
            QMessageBox::warning(this, QStringLiteral("加载失败"), msg);
            return false;
        }
        if (entry->cacheable)
            DatasetCache::save(path, entry->cacheNamespace, opts, ds);
    }


    const uint32_t n = ps_dataset_channel_count(ds);
    const QString fmt_tag = entry->formatTag;
    const int oldCount = m_ctx.sources().size();
    SourceData src;
    for (const ps::data_source::SourceData &old : m_ctx.sources())
        src.id = qMax(src.id, old.id + 1);
    src.name = fi.fileName();
    src.alias = fi.completeBaseName();
    for (const ps::data_source::SourceData &old : m_ctx.sources())
        if (old.alias == src.alias)
            src.alias += QStringLiteral(" #%1").arg(src.id + 1);
    src.path = fi.absoluteFilePath();
    src.fmt = fmt_tag;
    src.ds = ds;
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(ds, i))
            src.channels.append(ch);
    QVector<QStringList> logMsgs;
    if (fi.suffix().compare(QStringLiteral("ulg"), Qt::CaseInsensitive) == 0 && !src.channels.isEmpty())
        logMsgs = ulogLogMessages(src.path, src.channels.first()->time[0]);

    // 参数：由扩展提供 JSON，app 不认格式；可缓存时与数据集同 key 落盘。
    if (entry->hasParams && entry->paramsJson)
    {
        QString json;
        if (entry->cacheable)
            json = DatasetCache::loadParams(src.path, entry->cacheNamespace, opts);
        const bool fromCache = !json.isEmpty();
        if (!fromCache)
            json = entry->paramsJson(src.path);
        if (json.isEmpty())
        {
            m_ctx.log(QStringLiteral("参数表读取失败：%1").arg(entry->id));
        }
        else
        {
            if (!fromCache && entry->cacheable)
                DatasetCache::saveParams(src.path, entry->cacheNamespace, opts, json);
            for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array())
            {
                const QJsonObject o = v.toObject();
                src.params.append({o.value(QStringLiteral("name")).toString(),
                                    o.value(QStringLiteral("value")).toString()});
            }
            if (fromCache)
                m_ctx.log(QStringLiteral("参数缓存命中：%1").arg(fi.fileName()));
        }
    }

    // 消息：ULog 文本日志优先（带真实事件时间）；没有时按 topic 聚合。
    if (!logMsgs.isEmpty())
        src.msgs = logMsgs;
    else if (is_tree)
    {
        QMap<QString, QVector<const PsChannelInfo *>> byTopic;
        for (const PsChannelInfo *ch : src.channels)
        {
            const QString name = QString::fromUtf8(ch->name);
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
                byTopic[name.left(dot)].append(ch);
        }
        for (auto it = byTopic.constBegin(); it != byTopic.constEnd(); ++it)
        {
            QString hz = QStringLiteral("—");
            const PsChannelInfo *ch = it.value().isEmpty() ? nullptr : it.value().first();
            if (ch && ch->point_count > 1)
            {
                const double span = ch->time[ch->point_count - 1] - ch->time[0];
                if (span > 0)
                    hz = QStringLiteral("%1 Hz").arg((ch->point_count - 1) / span, 0, 'f',
                                                     (ch->point_count - 1) / span >= 10 ? 0 : 1);
            }
            const QString firstTime = (ch && ch->point_count > 0)
                ? QString::number(ch->time[0], 'f', 3)
                : QStringLiteral("—");
            src.msgs.append({it.key(), hz, QString::number(it.value().size()), firstTime});
        }
    }

    m_ctx.sources().append(src);
    addSourceRow(src.name, QStringLiteral("已加载 · %1 通道").arg(n), QStringLiteral("#7ee787"), true, src.path,
                 src.id);

    const bool firstSource = oldCount == 0;
    m_ctx.sourceLoaded(src, firstSource);

    m_ctx.statusMessage(QStringLiteral("%1 · %2 通道").arg(fi.fileName()).arg(n));
    m_ctx.recordRecent(fi.absoluteFilePath());

}
void DataSourceDock::connectLiveSource(const QString &extensionId)
{
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(extensionId);
    if (!sp)
    {
        m_ctx.log(QStringLiteral("实时源未注册：%1").arg(extensionId));
        return;
    }
    const auto parsers = DataSourceRegistry::instance().streamEntries();
    DataSourceDialog dlg(parsers, sp->id, QString(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList ids = dlg.selectedParserIds();
    if (ids.isEmpty())
        ids << dlg.primaryParserId();

    const int oldCount = m_ctx.sources().size();
    SourceData src;
    for (const ps::data_source::SourceData &old : m_ctx.sources())
        src.id = qMax(src.id, old.id + 1);
    QString note, err;
    if (!openLiveInput(ids, dlg.configJson(), src, &note, &err))
    {
        QMessageBox::warning(this, QStringLiteral("连接失败"), err);
        return;
    }
    m_ctx.sources().append(src);
    addSourceRow(src.name, QStringLiteral("运行中 · 0 通道"), QStringLiteral("#39c5cf"), true,
                 src.path, src.id);
    if (note.isEmpty())
        m_ctx.log(QStringLiteral("实时源已连接：%1").arg(src.fmt));
    else
        m_ctx.log(QStringLiteral("实时源已连接：%1；共享 transport 解析器：%2").arg(src.fmt, note));
}
bool DataSourceDock::openLiveInput(const QStringList &ids, const QString &config, ps::data_source::SourceData &dst,
                               QString *fanoutNote, QString *err)
{
    QStringList selected = ids;
    if (selected.isEmpty())
    {
        if (err) *err = QStringLiteral("没有选择解析器");
        return false;
    }
    const DataSourceEntry *primary = DataSourceRegistry::instance().findSourceById(selected.value(0));
    if (!primary)
    {
        if (err) *err = QStringLiteral("入口解析器未注册：%1").arg(selected.value(0));
        return false;
    }
    const QJsonObject liveConfig = QJsonDocument::fromJson(config.toUtf8()).object();
    auto session = std::make_shared<LiveSession>(primary->id, primary->label);
    if (liveConfig.value(QStringLiteral("__record")).toBool())
    {
        QString path, re;
        const auto format = StreamRecorder::parseFormat(
            liveConfig.value(QStringLiteral("__record_format")).toString());
        if (session->startRecording(format, config, &path, &re))
            m_ctx.log(QStringLiteral("实时记录（%1）：%2").arg(StreamRecorder::formatLabel(format), path));
        else
            QMessageBox::warning(this, QStringLiteral("记录"), re);
    }

    QVector<std::shared_ptr<FanoutParser>> extra;
    QStringList extraIds;
    for (int i = 1; i < selected.size(); ++i)
    {
        const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(selected[i]);
        if (!p || !p->feed)
        {
            m_ctx.log(QStringLiteral("解析器 %1 不支持共享 transport，已跳过").arg(selected.value(i)));
            continue;
        }
        QJsonObject sub = liveConfig;
        sub.insert(QStringLiteral("transport"), QStringLiteral("external"));
        sub.remove(QStringLiteral("filepath"));
        sub.remove(QStringLiteral("raw_path"));
        sub.remove(QStringLiteral("__replay_path"));
        QString re;
        void *handle = nullptr;
        auto ep = std::make_shared<FanoutParser>();
        ep->id = p->id;
        ep->session = session.get();
        ep->prefix = QStringLiteral("fanout.%1").arg(p->id);
        ep->sink.ctx = ep.get();
        ep->sink.channel_batch = &fanoutChannel;
        ep->sink.event = &fanoutEvent;
        if (p->open(QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact)),
                    &ep->sink, &handle, &re) != 0)
        {
            m_ctx.log(QStringLiteral("解析器 %1 启动失败：%2").arg(p->label, re));
            continue;
        }
        ep->handle = handle;
        extra.append(ep);
        extraIds << p->id;
    }

    if (!extra.isEmpty())
    {
        QVector<FanoutParser *> eps;
        for (const auto &ep : extra)
            eps.append(ep.get());
        session->setRawTap([eps](const char *topic, const char *encoding, int64_t ts,
                                 const uint8_t *data, uint32_t size) {
            for (FanoutParser *ep : eps)
            {
                const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(ep->id);
                QString re;
                if (p && p->feed)
                    p->feed(ep->handle, QString::fromUtf8(topic), QString::fromUtf8(encoding),
                            ts, QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(size)),
                            &re);
            }
        });
    }

    void *handle = nullptr;
    QString re;
    if (primary->open(config, session->sink(), &handle, &re) != 0)
    {
        for (const auto &ep : extra)
            if (const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(ep->id))
                if (ep->handle)
                    p->close(ep->handle);
        if (err) *err = QStringLiteral("%1\n%2").arg(primary->label, re);
        return false;
    }

    dst.name = QStringLiteral("%1 · 实时").arg(primary->label);
    dst.alias = QStringLiteral("%1_%2").arg(primary->kind).arg(dst.id + 1);
    dst.fmt = primary->label;
    dst.path = QStringLiteral("live://%1/%2").arg(primary->id).arg(dst.id);
    dst.live = true;
    dst.sourceId = primary->id;
    dst.config = config;
    dst.handle = handle;
    dst.liveSession = session;
    dst.fanout.clear();
    for (const auto &ep : extra)
        dst.fanout.append({ep->id, ep->handle});
    if (fanoutNote)
        *fanoutNote = extraIds.join(QStringLiteral(" | "));
    return true;
}
void DataSourceDock::configureLiveSource(ps::data_source::SourceData *src)
{
    if (!src || !src->live)
        return;
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src->sourceId);
    if (!sp)
        return;
    const auto parsers = DataSourceRegistry::instance().streamEntries();
    DataSourceDialog dlg(parsers, sp->id, src->config, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList ids = dlg.selectedParserIds();
    if (ids.isEmpty())
        ids << dlg.primaryParserId();

    closeLiveSource(src);
    m_ctx.removeSourceFromCanvases(src->path);
    src->liveSession.reset();
    src->channels.clear();
    src->channels.squeeze();
    src->fanout.clear();
    src->plotted = false;
    if (src->id == m_ctx.activeSourceId())
        m_ctx.clearSourceView();

    QString note, err;
    if (!openLiveInput(ids, dlg.configJson(), *src, &note, &err))
    {
        src->handle = nullptr;
        updateSourceMeta(src->row, QStringLiteral("已断开"), QStringLiteral("#4a545d"));
        QMessageBox::warning(this, QStringLiteral("连接失败"), err);
        return;
    }
    updateSourceMeta(src->row, QStringLiteral("运行中 · 0 通道"), QStringLiteral("#39c5cf"));
    m_ctx.refreshSourceView();
    if (note.isEmpty())
        m_ctx.log(QStringLiteral("实时源已重连：%1").arg(src->fmt));
    else
        m_ctx.log(QStringLiteral("实时源已重连：%1；共享 transport 解析器：%2").arg(src->fmt, note));
}
void DataSourceDock::pollLiveSources()
{
    for (ps::data_source::SourceData &src : m_ctx.sources())
    {
        if (!src.live || src.paused || !src.handle || !src.liveSession)
            continue;
        const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src.sourceId);
        if (!sp)
            continue;
        QString err;
        if (sp->poll(src.handle, 0, &err) != 0)
        {
            m_ctx.log(QStringLiteral("实时源 poll 失败：%1 %2").arg(sp->label, err));
            closeLiveSource(&src);
            continue;
        }
        if (src.liveSession->changed())
            refreshLiveSource(&src);
    }
}
void DataSourceDock::refreshLiveSource(ps::data_source::SourceData *src)
{
    if (!src || !src->liveSession)
        return;
    src->liveSession->clearChanged();
    src->channels = src->liveSession->channels();
    if (src->liveSession->overflow())
        return;
    updateSourceMeta(src->row, QStringLiteral("运行中 · %1 通道 · %2 点")
                                  .arg(src->channels.size())
                                  .arg(src->channels.isEmpty() ? 0 : src->channels.first()->point_count),
                     QStringLiteral("#39c5cf"));
    m_ctx.liveSourceChanged(src);

}
void DataSourceDock::closeLiveSource(ps::data_source::SourceData *src)
{
    if (!src || !src->live)
        return;
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src->sourceId);
    if (src->handle && sp)
        sp->close(src->handle);
    src->handle = nullptr;
    for (const auto &f : src->fanout)
        if (const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(f.first))
            if (f.second)
                p->close(f.second);
    src->fanout.clear();
    if (src->liveSession)
        src->liveSession->stopRecording();
    updateSourceMeta(src->row, QStringLiteral("已断开"), QStringLiteral("#4a545d"));
    refreshSrcCount();
}

void DataSourceDock::switchSource(int id)
{
    SourceData *src = m_ctx.sourceById(id);
    if (!src || (!src->ds && !src->live))
        return;
    m_ctx.setActiveSource(id);
    m_ctx.applySourceView(src);
}


void DataSourceDock::reloadChannels(ps::data_source::SourceData *src)
{
    if (!src)
        return;
    src->channels.clear();
    if (src->live && src->liveSession)
    {
        src->channels = src->liveSession->channels();
        return;
    }
    if (!src->ds)
        return;
    const uint32_t n = ps_dataset_channel_count(src->ds);
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(src->ds, i))
            src->channels.append(ch);
}
void DataSourceDock::refreshActiveSource()
{
    ps::data_source::SourceData *src = m_ctx.sourceById(m_ctx.activeSourceId());
    if (!src || (!src->ds && !src->live))
        return;
    reloadChannels(src);
    m_ctx.applySourceView(src);
}
void DataSourceDock::activateSource(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *w = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!w)
            continue;
        if (w->property("srcPanel").toBool())
            continue;
        const bool on = (w == row);
        if (w->property("active").toBool() != on)
        {
            w->setProperty("active", on);
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
    const int id = row->property("srcId").toInt();
    if (row->property("backed").toBool() && m_ctx.sourceById(id))
    {
        switchSource(id);
        m_ctx.showSourceProps(row);
        return;
    }
    m_ctx.clearSourceView();
    m_ctx.refreshSourceView();
    m_ctx.showSourceProps(row);
}
void DataSourceDock::setSourcePaused(ps::data_source::SourceData *src, bool paused)
{
    if (!src || !src->live)
        return;
    src->paused = paused;
    if (src->row)
    {
        if (QLabel *meta = src->row->findChild<QLabel *>(QStringLiteral("srcMeta")))
        {
            const QString base = src->row->property("srcMeta").toString();
            meta->setText(paused ? QStringLiteral("已暂停 · %1").arg(base) : base);
        }
        if (QToolButton *btn = src->row->findChild<QToolButton *>(QStringLiteral("srcPause")))
        {
            btn->setIcon(QIcon(paused ? QStringLiteral(":/icons/play.svg")
                                      : QStringLiteral(":/icons/pause.svg")));
            btn->setToolTip(paused ? QStringLiteral("继续该实时源") : QStringLiteral("暂停该实时源"));
        }
    }
    m_ctx.refreshStatusChip();
    m_ctx.log(QStringLiteral("%1实时源：%2").arg(paused ? QStringLiteral("暂停") : QStringLiteral("继续"), src->name));
}
void DataSourceDock::exportActiveCsv()
{
    ps::data_source::SourceData *src = m_ctx.sourceById(m_ctx.activeSourceId());
    if (!src || !src->ds)
    {
        m_ctx.log(QStringLiteral("没有可导出的数据源"));
        return;
    }
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出数据快照"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(src->alias + QStringLiteral("-snapshot.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty())
        return;
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("导出失败"), out.errorString());
        return;
    }
    QTextStream ts(&out);
    ts.setEncoding(QStringConverter::Utf8);
    auto esc = [](const QString &v) {
        QString s = v;
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))
            ? QStringLiteral("\"%1\"").arg(s) : s;
    };
    const int sampleCap = QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
                              .value(QStringLiteral("exportSampleLimit"), 0).toInt();
    ts << "time,source,channel,value,unit\n";
    qulonglong rows = 0;
    const QString alias = esc(src->alias);
    for (const PsChannelInfo *ch : src->channels)
    {
        if (!ch)
            continue;
        const QString name = esc(QString::fromUtf8(ch->name));
        const QString unit = esc(QString::fromUtf8(ch->unit ? ch->unit : ""));
        const uint32_t step = sampleCap > 0 && ch->point_count > static_cast<uint32_t>(sampleCap)
                                  ? (ch->point_count + static_cast<uint32_t>(sampleCap) - 1) /
                                        static_cast<uint32_t>(sampleCap)
                                  : 1;
        for (uint32_t i = 0; i < ch->point_count; i += step)
        {
            ts << QString::number(ch->time[i], 'g', 17) << ',' << alias << ',' << name << ','
               << QString::number(ch->value[i], 'g', 17) << ',' << unit << '\n';
            ++rows;
        }
    }
    out.close();
    m_ctx.log(QStringLiteral("已导出数据快照：%1（%2 行）").arg(f).arg(rows)
        + (sampleCap > 0 ? QStringLiteral("；采样上限 %1 点/通道").arg(sampleCap) : QString()));
}
void DataSourceDock::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    if (!row)
        return;
    const bool backed = row->property("backed").toBool();
    const QString path = row->property("srcPath").toString();

    QMenu menu(this);
    ps::data_source::SourceData *pauseSrc = m_ctx.sourceById((row ? row->property("srcId").toInt() : -1));
    const bool live = pauseSrc && pauseSrc->live;
    QAction *toggle = menu.addAction(live
        ? (pauseSrc->paused ? QStringLiteral("继续数据源") : QStringLiteral("暂停数据源"))
        : QStringLiteral("开始数据源"));
    toggle->setEnabled(live);
    QAction *reload = menu.addAction(QStringLiteral("重新加载"));
    reload->setEnabled(!path.isEmpty());
    QAction *rename = menu.addAction(QStringLiteral("重命名"));

    ps::data_source::SourceData *src = m_ctx.sourceById((row ? row->property("srcId").toInt() : -1));
    QMenu *timeMenu = menu.addMenu(QStringLiteral("时间对齐"));
    timeMenu->setEnabled(backed && src && src->ds);
    QAction *offsetInfo = timeMenu->addAction(
        QStringLiteral("偏移 %1 s").arg(src ? src->offsetSec : 0.0, 0, 'f', 3));
    offsetInfo->setEnabled(false);
    timeMenu->addSeparator();
    QAction *alignStart = timeMenu->addAction(QStringLiteral("对齐起点"));
    QAction *alignCenter = timeMenu->addAction(QStringLiteral("对齐中心"));
    QAction *alignEnd = timeMenu->addAction(QStringLiteral("对齐终点"));
    QAction *base = timeMenu->addAction(QStringLiteral("设为时间基准"));
    timeMenu->addSeparator();
    QAction *resetOffset = timeMenu->addAction(QStringLiteral("重置偏移"));

    QMenu *cmpMenu = menu.addMenu(QStringLiteral("对比同名通道"));
    bool hasOther = false;
    for (ps::data_source::SourceData &o : m_ctx.sources())
    {
        if (!src || o.id == src->id || !o.ds)
            continue;
        hasOther = true;
        QMenu *sub = cmpMenu->addMenu(o.alias.isEmpty() ? o.name : o.alias);
        QAction *all = sub->addAction(QStringLiteral("叠加全部同名"));
        all->setProperty("cmpAll", true);
        all->setProperty("cmpOther", o.id);
        QAction *one = sub->addAction(QStringLiteral("选择通道…"));
        one->setProperty("cmpAll", false);
        one->setProperty("cmpOther", o.id);
    }
    cmpMenu->setEnabled(hasOther);
    menu.addSeparator();
    QAction *info = menu.addAction(QStringLiteral("数据源信息"));
    QAction *del = menu.addAction(QStringLiteral("移除数据源"));
    QAction *delAll = menu.addAction(QStringLiteral("移除全部数据源"));

    QAction *chosen = menu.exec(globalPos);
    if (chosen == delAll)
    {
        QList<QWidget *> rows;
        for (int i = 0; i < m_srcRows->count(); ++i)
            if (QWidget *row = m_srcRows->itemAt(i)->widget())
                rows.append(row);
        for (QWidget *row : rows)
            removeSourceRow(row);
        return;
    }
    if (chosen == toggle)
    {
        if (ps::data_source::SourceData *src = m_ctx.sourceById((row ? row->property("srcId").toInt() : -1)))
            m_ctx.setSourcePaused(src, !src->paused);
    }
    else if (chosen == reload)
    {
        m_ctx.log(QStringLiteral("重新加载：%1").arg(path));
        m_ctx.reloadFile(path);
    }
    else if (chosen == info)
    {
        if (!src)
            return;
        const int channels = src->live
            ? (src->liveSession ? src->liveSession->channels().size() : 0)
            : (src->ds ? static_cast<int>(ps_dataset_channel_count(src->ds)) : 0);
        const int points = src->live
            ? (src->liveSession ? src->liveSession->totalSamples() : 0)
            : (src->ds && ps_dataset_channel(src->ds, 0)
                   ? static_cast<int>(ps_dataset_channel(src->ds, 0)->point_count) : 0);
        QStringList lines;
        lines << QStringLiteral("名称：%1").arg(src->alias.isEmpty() ? src->name : src->alias)
              << QStringLiteral("类型：%1").arg(src->live ? QStringLiteral("实时源") : src->fmt)
              << QStringLiteral("路径：%1").arg(src->path.isEmpty() ? src->config : src->path)
              << QStringLiteral("通道：%1").arg(channels)
              << QStringLiteral("首通道点数：%1").arg(points)
              << QStringLiteral("参数：%1 · 消息：%2").arg(src->params.size()).arg(src->msgs.size())
              << QStringLiteral("时间偏移：%1 s").arg(src->offsetSec, 0, 'f', 3);
        QMessageBox::information(this, QStringLiteral("数据源信息"), lines.join(QLatin1Char('\n')));
    }
    else if (chosen == rename)
    {
        QList<QLabel *> labels = row->findChildren<QLabel *>();
        QLabel *name = labels.value(1, nullptr);
        if (!name)
            return;
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("重命名数据源"),
                                                   QStringLiteral("名称"), QLineEdit::Normal,
                                                   name->text(), &ok);
        if (ok && !text.trimmed().isEmpty())
        {
            name->setText(text.trimmed());
            row->setProperty("srcName", text.trimmed());
            m_ctx.refreshPropTree();
            ps::data_source::SourceData *src = m_ctx.sourceById((row ? row->property("srcId").toInt() : -1));
            if (src)
            {
                src->alias = text.trimmed();
                m_ctx.renameSource(src, text.trimmed());
            }
            m_ctx.log(QStringLiteral("重命名数据源：%1").arg(text.trimmed()));
        }
    }
    else if (chosen == alignStart || chosen == alignCenter || chosen == alignEnd)
    {
        m_ctx.alignSource(src, chosen == alignStart ? 0 : (chosen == alignEnd ? 2 : 1));
    }
    else if (chosen == base)
    {
        m_ctx.setSourceBase(src);
    }
    else if (chosen == resetOffset)
    {
        m_ctx.setSourceOffset(src, 0.0, true);
        m_ctx.log(QStringLiteral("重置时间偏移：%1").arg(src ? src->alias : QString()));
    }
    else if (chosen && chosen->property("cmpOther").isValid())
    {
        ps::data_source::SourceData *other = m_ctx.sourceById(chosen->property("cmpOther").toInt());
        m_ctx.compareSources(src, other, chosen->property("cmpAll").toBool(), QString());
    }
    else if (chosen == del)
    {
        removeSourceRow(row);
    }
}

void DataSourceDock::removeSourceRow(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    const QString name = row->property("srcName").toString();
    const int id = (row ? row->property("srcId").toInt() : -1);
    ps::data_source::SourceData *src = m_ctx.sourceById(id);
    if (src)
    {
        const QString path = src->path;
        m_ctx.removeSourceFromCanvases(path);
        m_ctx.closeLiveSource(src);
        if (src->ds)
            ps_dataset_free(src->ds);
        if (m_ctx.activeSourceId() == id)
        {
            m_ctx.setActiveSource(-1);
            m_ctx.clearSourceView();
        }
        int idx = -1;
        for (int i = 0; i < m_ctx.sources().size(); ++i)
            if (m_ctx.sources().at(i).id == id)
                idx = i;
        if (idx >= 0)
            m_ctx.sources().removeAt(idx);
        m_ctx.refreshSourceView();
        // 删掉当前源后落到剩下的第一个可绘源
        for (const ps::data_source::SourceData &other : m_ctx.sources())
        {
            if (other.ds && other.row)
            {
                m_ctx.activateSource(other.row);
                break;
            }
        }
    }
    if (QWidget *host = m_srcRows->parentWidget())
        if (auto *panel = host->findChild<ChannelTree *>(
                QStringLiteral("srcPanel_%1").arg(id)))
        {
            m_srcRows->removeWidget(panel);
            panel->deleteLater();
        }
    m_srcRows->removeWidget(row);
    row->deleteLater();
    refreshSrcCount();
    if (m_ctx.sources().isEmpty())
        m_ctx.refreshPropTree();
    m_ctx.log(QStringLiteral("移除数据源：%1").arg(name));
}


void DataSourceDock::renderSourceData(const QVector<const PsChannelInfo *> &channels,
                                      const QVector<QPair<QString, QString>> &params,
                                      const QVector<QStringList> &msgs,
                                      const QString &fmt, bool backed, int activeId)
{
    if (!m_srcTree || !m_srcTabs)
        return;
    m_srcTree->clear();
    const int n = backed ? channels.size() : 0;
    syncSrcChips();
    updateSrcStatus();

    if (!(m_ctx.hasSources ? m_ctx.hasSources() : true) && m_srcEmpty)
    {
        m_srcTabs->setTabText(0, QStringLiteral("字段 0"));
        m_srcTabs->setTabText(1, QStringLiteral("参数 0"));
        m_srcTabs->setTabText(2, QStringLiteral("消息 0"));
        syncSrcChips();
        m_srcStack->setCurrentWidget(m_srcEmpty);
        return;
    }
    if (m_srcStack && m_srcEmpty && m_srcStack->currentWidget() == m_srcEmpty)
        m_srcStack->setCurrentIndex(m_srcTabs->currentIndex());

    if (!backed)
    {
        auto *it = new QTreeWidgetItem(m_srcTree, {QStringLiteral("该数据源暂未提供字段")});
        it->setFlags(Qt::NoItemFlags);
        m_srcTabs->setTabText(0, QStringLiteral("字段 0"));
        m_srcTabs->setTabText(1, QStringLiteral("参数 0"));
        m_srcTabs->setTabText(2, QStringLiteral("消息 0"));

        m_paramTable->setRowCount(1);
        auto *pk = new QTableWidgetItem(QStringLiteral("—"));
        auto *pv = new QTableWidgetItem(QStringLiteral("该数据源未接入，没有参数表"));
        pk->setFlags(Qt::ItemIsEnabled);
        pv->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(0, 0, pk);
        m_paramTable->setItem(0, 1, pv);

        m_msgTable->setRowCount(1);
        for (int c = 0; c < 4; ++c)
        {
            auto *mi = new QTableWidgetItem(c == 0 ? QStringLiteral("—") : QString());
            mi->setFlags(Qt::ItemIsEnabled);
            m_msgTable->setItem(0, c, mi);
        }
        return;
    }

    const bool is_tree = fmt == QStringLiteral("ULog")
                      || fmt == QStringLiteral("ArduPilot · BIN")
                      || fmt.startsWith(QStringLiteral("ASC"))
                      || fmt.startsWith(QStringLiteral("BLF"));
    QMap<QString, QTreeWidgetItem *> groups;
    ps::data_source::SourceData *active = m_ctx.sourceById(activeId);
    QTreeWidgetItem *derivedGroup = nullptr;
    for (const PsChannelInfo *ch : channels)
    {
        const QString name = QString::fromUtf8(ch->name);
        QString group;
        QString leaf = name;
        const bool derived = active && active->derived.contains(name);
        if (derived)
        {
            if (!derivedGroup)
            {
                derivedGroup = new QTreeWidgetItem(m_srcTree, {QStringLiteral("派生通道")});
                derivedGroup->setForeground(0, QColor(QStringLiteral("#f2b134")));
            }
            auto *item = new QTreeWidgetItem(derivedGroup, {name.mid(name.lastIndexOf(QLatin1Char('.')) + 1)});
            item->setToolTip(0, name);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
            item->setData(0, Qt::UserRole + 1, name);
            continue;
        }
        if (is_tree)
        {
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
            {
                group = name.left(dot);
                leaf = name.mid(dot + 1);
            }
        }
        QTreeWidgetItem *parent = nullptr;
        if (!group.isEmpty())
        {
            auto it = groups.find(group);
            if (it == groups.end())
            {
                auto *g = new QTreeWidgetItem(m_srcTree, {group});
                it = groups.insert(group, g);
            }
            parent = it.value();
        }
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, {leaf})
                                       : new QTreeWidgetItem(m_srcTree, {leaf});
        item->setToolTip(0, name);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        item->setData(0, Qt::UserRole + 1, name);
    }
    const bool few_groups = groups.size() <= 8;
    for (auto *g : groups)
        g->setExpanded(few_groups && is_tree);
    if (is_tree && !few_groups && !groups.isEmpty())
        groups.first()->setExpanded(true);
    if (derivedGroup)
        derivedGroup->setExpanded(true);

    m_srcTabs->setTabText(0, QStringLiteral("字段 %1").arg(n));
    m_srcTabs->setTabText(1, QStringLiteral("参数 %1").arg(params.size()));
    m_srcTabs->setTabText(2, QStringLiteral("消息 %1").arg(msgs.size()));
    syncSrcChips();

    m_paramTable->setRowCount(params.size());
    for (int i = 0; i < params.size(); ++i)
    {
        auto *k = new QTableWidgetItem(params[i].first);
        auto *v = new QTableWidgetItem(params[i].second);
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_paramTable->setItem(i, 0, k);
        m_paramTable->setItem(i, 1, v);
    }
    if (params.isEmpty())
    {
        m_paramTable->setRowCount(1);
        auto *k = new QTableWidgetItem(QStringLiteral("—"));
        auto *v = new QTableWidgetItem(QStringLiteral("%1 未提供参数表（仅 ULog 带 P/Q 参数消息）").arg(fmt));
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_paramTable->setItem(0, 0, k);
        m_paramTable->setItem(0, 1, v);
    }

    m_msgTable->setRowCount(msgs.size());
    for (int i = 0; i < msgs.size(); ++i)
    {
        for (int c = 0; c < 4; ++c)
        {
            auto *mi = new QTableWidgetItem(msgs[i].value(c));
            mi->setFlags(Qt::ItemIsEnabled);
            mi->setForeground(QColor(c == 0 ? QStringLiteral("#d5dde5") : QStringLiteral("#9aa7b2")));
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(i, c, mi);
        }
    }
    if (msgs.isEmpty())
    {
        m_msgTable->setRowCount(1);
        for (int c = 0; c < 4; ++c)
        {
            auto *mi = new QTableWidgetItem(QStringLiteral("—"));
            mi->setFlags(Qt::ItemIsEnabled);
            mi->setForeground(QColor(c == 0 ? QStringLiteral("#d5dde5") : QStringLiteral("#9aa7b2")));
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(0, c, mi);
        }
    }
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
        const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(rowSrc->sourceId);
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
