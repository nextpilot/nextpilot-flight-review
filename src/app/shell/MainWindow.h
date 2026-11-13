#pragma once

#include <QMainWindow>
#include <QJsonObject>

struct PsDataset;
struct PsChannelInfo;
class CanvasWidget;
class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTabBar;
class QTreeWidget;
class QTableWidget;
class QStackedWidget;
class QListWidget;
class QVBoxLayout;
class QLineEdit;

// 变换配方：写入 .psl，重启/导入时按序重放
struct TransformRecipe
{
    QString kind;             // filter / quat
    QString input;
    QString filter;
    QJsonObject params;
    QString output;           // filter 输出名 / quat 输出主题
    QString quat[4];
    bool degrees = true;
    bool unwrap = true;
};

// 一个数据源：各自持有内核数据集，切源不释放，删除源才释放
struct SourceData
{
    int id = -1;
    QString name;
    QString alias;             // 图例前缀，默认文件名，可重命名
    QString path;
    QString fmt;
    double offsetSec = 0.0;    // 显示时间 = 原始时间 - offset；多源对齐用
    PsDataset *ds = nullptr;
    QVector<const PsChannelInfo *> channels;
    QVector<QPair<QString, QString>> params;
    QVector<QStringList> msgs;
    QStringList derived;       // 本源由变换产生的通道名，右键才允许删除
    QVector<TransformRecipe> recipes;
    QWidget *row = nullptr;
};

// 主窗口骨架（对应 §8 / D14）：
//   Ribbon（顶部任务页签） + 四个可停靠区（数据源 / 属性 / 脚本 / 日志） + 固定绘图区（页签=页）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 打开一份 CSV（内核解析），把前 3 个通道画到当前页画布
    bool loadFile(const QString &path, bool interactive = true);
    void importLayoutFrom(const QString &path);
    void saveLayoutTo(const QString &path);
    QString defaultLayoutPath() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    CanvasWidget *currentCanvas() const;
    QVector<CanvasWidget *> pageCanvases() const;
    QWidget *createRibbon();
    void createDocks();
    void createPages();
    void log(const QString &text);
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);
    QWidget *addSourceRow(const QString &name, const QString &meta, const QString &dotColor,
                          bool backed = false, const QString &path = QString(), int srcId = -1);
    void showSourceMenu(QWidget *row, const QPoint &globalPos);
    void removeSourceRow(QWidget *row);
    void activateSource(QWidget *row);   // 每个源各自一份字段/参数/消息视图
    void renderSourceData();
    void refreshActiveSource();          // 变换写回后重建通道指针与字段树
    void reloadChannels(SourceData *src);
    void addTransformFor(const QString &channel);
    void setSourceOffset(SourceData *src, double offsetSec, bool refit);
    void alignSource(SourceData *src, int mode);          // 0 起点 / 1 中心 / 2 终点
    void setSourceBase(SourceData *src);
    void compareSources(SourceData *src, SourceData *other, bool all, const QString &pick);
    void setupCanvas(CanvasWidget *c);
    void addCanvasToPage();
    void removeCanvas(CanvasWidget *c);
    void openSettings();
    QJsonObject pageState(QWidget *page) const;
    QWidget *buildPage(const QJsonObject &o);
    int sourceIdForRow(QWidget *row) const;
    SourceData *sourceById(int id);
    void switchSource(int id);
    void refreshSrcCount();
    void applySrcFilter();
    void newPage();

    QTabWidget *m_pages = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QTreeWidget *m_srcTree = nullptr;      // 字段页：通道树（按 topic 分组）
    QTabBar *m_srcTabs = nullptr;          // 字段 / 参数 / 消息
    QStackedWidget *m_srcStack = nullptr;
    QLabel *m_srcCount = nullptr;          // “3 源 · 2 已连接”
    bool m_viewBacked = false;            // 当前选中的数据源是否持有内核数据集
    QString m_srcFmt;                     // 当前源的格式标签（ULog/CSV/...）
    QVector<const PsChannelInfo *> m_srcChannels;
    QVector<QPair<QString, QString>> m_srcParams;
    QVector<QStringList> m_srcMsgs;
    QLineEdit *m_srcFilter = nullptr;      // 字段/参数/消息 共用的搜索框
    QString m_kernelInfo;                  // 状态栏常驻信息（Ribbon 悬停时临时替换）
    QTableWidget *m_paramTable = nullptr;
    QTableWidget *m_msgTable = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QTableWidget *m_propTable = nullptr;
    QLabel *m_selLabel = nullptr;
    QLabel *m_runLabel = nullptr;
    QPlainTextEdit *m_script = nullptr;
    QVector<SourceData> m_sources;
    int m_activeSource = -1;
};
