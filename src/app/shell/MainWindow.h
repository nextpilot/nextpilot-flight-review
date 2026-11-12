#pragma once

#include <QMainWindow>

struct PsDataset;
class CanvasWidget;
class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTabBar;
class QTreeWidget;
class QTableWidget;

// 主窗口骨架（对应 §8 / D14）：
//   Ribbon（顶部任务页签） + 四个可停靠区（数据源 / 属性 / 脚本 / 日志） + 固定绘图区（页签=页）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 打开一份 CSV（内核解析），把前 3 个通道画到当前页画布
    bool loadFile(const QString &path);

private:
    CanvasWidget *currentCanvas() const;
    QVector<CanvasWidget *> pageCanvases() const;
    QWidget *createRibbon();
    void createDocks();
    void createPages();
    void log(const QString &text);
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);

    QTabWidget *m_pages = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QTreeWidget *m_srcTree = nullptr;
    QTableWidget *m_propTable = nullptr;
    QLabel *m_selLabel = nullptr;
    QLabel *m_runLabel = nullptr;
    QPlainTextEdit *m_script = nullptr;
    PsDataset *m_dataset = nullptr;
};
