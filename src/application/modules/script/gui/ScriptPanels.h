#pragma once

#include <QJsonObject>
#include <QPlainTextEdit>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QVector>
#include <QListWidgetItem>
#include <QWidget>
#include <functional>

class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTabWidget;
class QLineEdit;
class QTimer;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::script
{

class LuaHighlighter;

// ============================================================================
// 脚本工作台：四个独立 ADS 停靠面板（挂 app 主窗口，可自由停靠/浮动/分页）。
//
//   script-files   PrimarySidebar    File Explorer（脚本库 + 新建/删除）
//   script-editor  Editor            Code Editor（多标签；文档模型/运行/调试都在这）
//   script-debug   SecondarySidebar  Stack + Variables + API 速查
//   script-console Panel             Command/REPL（持久 VM）
//
// 面板间协作零新机制：全部走既有 viewAction 通道
// （InteractionHub::registerViewAction / dispatchViewAction）：
//   script.open  {name}                      files → editor   打开脚本到新标签
//   script.new   {name}                      files → editor   新建未保存文档
//   script.closed {name}                     files → editor   同步关闭对应标签
//   script.goto  {name, line}                console/editor → editor  行号跳转
//   script.insert {text}                     debug → editor   光标处插入调用模板
//   script.console.append {text, error}      editor → console 输出回显
//   script.debug.update {paused, name, line,
//                        locals, stack}      editor → debug   暂停快照刷新
//   script.debug.clear {}                    editor → debug   运行结束清快照
//   script.debug.pause {name, line}          module → editor  调试暂停行高亮跳转
//   script.debug.done {}                     module → editor  调试结束解除高亮
//   script.editor.run {debug?}               module → editor  执行当前标签（Ribbon 同路径）
//
// 实例由 PS_REGISTER_PANEL 工厂创建（GUI 专属，headless 不链）。
// ============================================================================

// ---------------------------------------------------------------------------
// 编辑器薄壳：QPlainTextEdit + 自绘 gutter（行号/断点圆点/错误行标记）
// + 当前行高亮。断点集挂在编辑器上（每文档一份，多标签天然隔离）。
// ---------------------------------------------------------------------------
class ScriptCodeEditor : public QPlainTextEdit
{
public:
    explicit ScriptCodeEditor(QWidget *parent = nullptr);
    ~ScriptCodeEditor() override;

    const QSet<int> &breakpoints() const { return m_breakpoints; }
    void setErrorLine(int line); // -1 清除
    void setDebugLine(int line); // 调试暂停行高亮（黄），-1 清除
    void jumpToLine(int line);   // 滚动并置光标
    void setBreakpoints(const QSet<int> &bps) { m_breakpoints = bps; update(); }

    std::function<void(int)> onBreakpointToggle; // gutter 点击切换断点
    QSyntaxHighlighter *highlighter = nullptr;   // 由面板按语言注入（Lua/Python）

    // gutter 绘制（GutterWidget 是实现细节，同文件内回调）。
    void paintGutter(QPaintEvent *ev);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override; // gutter 点击 → 断点
    void keyPressEvent(QKeyEvent *ev) override;          // Ctrl+Tab 切标签上浮给面板

private:
    QWidget *m_gutter;
    QSet<int> m_breakpoints;
    int m_errorLine = -1;
    int m_debugLine = -1; // 调试暂停行
    int gutterWidth() const;
    void updateGutterWidth();
    void refreshExtraSelections(); // 当前行/错误行高亮
};

// ---------------------------------------------------------------------------
// script-editor：多标签代码编辑器 + 运行/调试。文档模型与引擎都在这里。
// ---------------------------------------------------------------------------
class ScriptEditorPanel final : public QWidget
{
    Q_OBJECT
public:
    ScriptEditorPanel(context::AppContext &appCtx,
                      nextpilot::plotscope::intent::IIntentExecutor &executor,
                      QWidget *parent = nullptr);
    ~ScriptEditorPanel() override;

    // 供 Ribbon/意图与测试使用
    int documentCount() const;
    QString currentDocumentName() const;
    bool openScript(const QString &name);          // 打开脚本库脚本（不存在 false）
    bool newDocument(const QString &name, const QString &initialText = QString()); // 新建未保存文档（initialText 非空时代替模板）
    bool closeDocumentByName(const QString &name); // 脚本库删除后同步关标签
    bool saveCurrentDocument();
    void gotoLine(const QString &name, int line);
    void insertText(const QString &text);

private slots:
    void onRunCurrent(bool debugMode);
    void onStop();
    void onTabCloseRequested(int index);
    void onTabChanged(int index);
    void onEditorModified();

private:
    struct Document
    {
        ScriptCodeEditor *editor = nullptr;
        QString name; // 脚本库名（不含后缀）；空 = 未保存
        bool dirty = false;
        QString draftId; // 未保存文档的草稿标识；首次落草稿时生成
    };

    Document *currentDoc();
    Document *docAt(int index);
    Document *docByName(const QString &name);
    int openDocument(Document doc, const QString &title);
    void markDirty(Document *doc, bool dirty);
    void updateTabTitle(int index);
    bool ensureSavedName(Document *doc); // 无名文档先询问并登记
    void appendConsole(const QString &text, bool isError);
    void reportErrorLine(const QString &message); // 解析 name:line 跳转高亮
    void attachViewActions();
    void dispatchDebugCmd(const QString &cmd); // 调试控制经模块通道（continue/stepover/...）

    // 会话与草稿：标签顺序/光标/滚动/断点进快照段；未保存正文落 drafts 目录，
    // 崩溃后由 index.json 对账找回。
    QJsonObject serializeSession() const;
    void restoreSession(const QJsonObject &state);
    QString draftDir() const;
    void writeDrafts();       // 脏文档正文 + 索引整份重写
    void clearDrafts();       // 正常退出时清空，残留即代表上次异常退出
    void reconcileDrafts();   // 启动对账：index 里剩下的问用户是否还原

    context::AppContext *m_app = nullptr;
    nextpilot::plotscope::intent::IIntentExecutor *m_executor = nullptr;

    QTabWidget *m_tabs = nullptr;
    QVector<Document> m_docs;

    // 调试状态（2.1.7-2.1.9 接线；v1 先备好结构）
    bool m_debugging = false;
    bool m_stopRequested = false;
    QTimer *m_draftTimer = nullptr;
};

// ---------------------------------------------------------------------------
// script-console：REPL 控制台。**引擎不在面板**：求值经 viewAction
// script.workbench.repl 发给 ScriptModule（持久 VM 归模块持有，层契约要求）；
// print/app.log 输出经 script.console.append 回显；错误行号可点击跳转。
// ---------------------------------------------------------------------------
class ScriptConsolePanel final : public QWidget
{
    Q_OBJECT
public:
    ScriptConsolePanel(context::AppContext &appCtx,
                       nextpilot::plotscope::intent::IIntentExecutor &executor,
                       QWidget *parent = nullptr);
    ~ScriptConsolePanel() override;

    // 供 editor 面板回显运行输出（经 viewAction script.console.append 到这）
    void append(const QString &text, bool isError);

private slots:
    void onSubmit();

private:
    bool eventFilter(QObject *obj, QEvent *ev) override; // 输出区点击 → 行号跳转

    context::AppContext *m_app = nullptr;
    nextpilot::plotscope::intent::IIntentExecutor *m_executor = nullptr;
    QPlainTextEdit *m_out = nullptr;
    QLineEdit *m_input = nullptr;
};

// ---------------------------------------------------------------------------
// script-files：File Explorer。脚本库列表 + 新建/删除/刷新。
// ---------------------------------------------------------------------------
class ScriptFilesPanel final : public QWidget
{
    Q_OBJECT
public:
    ScriptFilesPanel(context::AppContext &appCtx,
                     nextpilot::plotscope::intent::IIntentExecutor &executor,
                     QWidget *parent = nullptr);

private slots:
    void onNew();
    void onDelete();
    void onOpen(QListWidgetItem *item);
    void onRefresh();

private:
    context::AppContext *m_app = nullptr;
    QListWidget *m_list = nullptr;
};

// ---------------------------------------------------------------------------
// script-debug：Stack + Variables（调试暂停时刷新）+ API 速查页签。
// ---------------------------------------------------------------------------
class ScriptDebugPanel final : public QWidget
{
    Q_OBJECT
public:
    ScriptDebugPanel(context::AppContext &appCtx,
                     nextpilot::plotscope::intent::IIntentExecutor &executor,
                     QWidget *parent = nullptr);

    // editor → debug：暂停快照（params: paused/name/line/locals/stack）
    void applyUpdate(const QVariantMap &params);
    void clearPause();

private slots:
    void onInsertApi(QListWidgetItem *item);

private:
    context::AppContext *m_app = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_debugTree = nullptr; // 调用栈 + 局部变量
    QListWidget *m_apiList = nullptr;   // op 目录（意图层实时导出）
};

} // namespace nextpilot::plotscope::script
