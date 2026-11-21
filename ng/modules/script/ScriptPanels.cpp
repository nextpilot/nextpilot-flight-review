#include "ScriptPanels.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "InteractionRegistry.h"
#include "PanelFactoryRegistry.h"
#include "Result.h"
#include "ErrorCodes.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTreeWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

using context::AppContext;
using intent::IIntentExecutor;
using context::InteractionRegistry;

namespace
{
// 面板间 viewAction id（零新机制：全部走既有 viewAction 通道）。
const QLatin1String kActOpen("script.open");
const QLatin1String kActNew("script.new");
const QLatin1String kActClosed("script.closed");
const QLatin1String kActGoto("script.goto");
const QLatin1String kActInsert("script.insert");
const QLatin1String kActConsoleAppend("script.console.append");
const QLatin1String kActDebugUpdate("script.debug.update");
const QLatin1String kActDebugClear("script.debug.clear");
const QLatin1String kActDebugPause("script.debug.pause");   // module → editor 暂停行高亮
const QLatin1String kActDebugDone("script.debug.done");     // module → editor 解除高亮
const QLatin1String kActEditorRun("script.editor.run");     // module → editor 执行当前标签
// 执行通道：处理函数由 ScriptModule::registerWorkbenchActions 注册（引擎归模块）。
const QLatin1String kActRun("script.workbench.run");
const QLatin1String kActRepl("script.workbench.repl");
const QLatin1String kActDebugRun("script.workbench.debug");
const QLatin1String kActDebugCmd("script.workbench.debug."); // 控制通道前缀

// 脚本名规则：小写字母/数字/下划线/短横线，1..64；可选 ".py" 后缀（M2.6 多语言）。
bool validScriptName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_-]{1,60}(\\.py)?$"));
    return re.match(name).hasMatch();
}

// 文档语言：名字以 .py 结尾 → Python（运行路由 script.py.exec + Python 高亮）。
bool isPythonDoc(const QString &name)
{
    return name.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive);
}

QString variantToDisplay(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return QStringLiteral("nil");
    switch (v.typeId())
    {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double:
        return v.toString();
    case QMetaType::QString:
        return QStringLiteral("\"%1\"").arg(v.toString());
    case QMetaType::QVariantMap:
        return QStringLiteral("{table: %1 项}").arg(v.toMap().size());
    case QMetaType::QVariantList:
        return QStringLiteral("{array: %1 项}").arg(v.toList().size());
    default:
        return v.toString();
    }
}

// 从错误消息里提取 (chunkName, line)。"repl:3: ..." / "@name.lua:12: ..."。
bool parseErrorLocation(const QString &message, QString *nameOut, int *lineOut)
{
    static const QRegularExpression re(QStringLiteral("(?:@?)([\\w./-]+):(\\d+):"));
    const QRegularExpressionMatch m = re.match(message);
    if (!m.hasMatch())
        return false;
    if (nameOut)
        *nameOut = m.captured(1);
    if (lineOut)
        *lineOut = m.captured(2).toInt();
    return true;
}
} // namespace

// ============================================================================
// LuaHighlighter：Lua 语法高亮（.py 将来换 PythonHighlighter，接口相同）
// ============================================================================
class LuaHighlighter final : public QSyntaxHighlighter
{
public:
    explicit LuaHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
    {
        // 关键字（紫）
        QTextCharFormat kw;
        kw.setForeground(QColor(0x7c, 0x3a, 0xed));
        kw.setFontWeight(QFont::Bold);
        m_rules.append({QRegularExpression(
                            QStringLiteral("\\b(local|function|end|if|then|else|elseif|for|while|do|"
                                           "return|break|nil|true|false|and|or|not|in|repeat|until)\\b")),
                        kw});
        // 字符串（绿）
        QTextCharFormat str;
        str.setForeground(QColor(0x05, 0x96, 0x69));
        m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'")),
                        str});
        // 数字（橙）
        QTextCharFormat num;
        num.setForeground(QColor(0xd9, 0x77, 0x06));
        m_rules.append({QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")), num});
        // 全局对象（蓝粗）：app / ps / ctx
        QTextCharFormat glob;
        glob.setForeground(QColor(0x25, 0x63, 0xeb));
        glob.setFontWeight(QFont::Bold);
        m_rules.append({QRegularExpression(QStringLiteral("\\b(app|ps|ctx)\\b")), glob});
        // 注释（灰斜体，最后套用保证覆盖）
        QTextCharFormat cmt;
        cmt.setForeground(QColor(0x9c, 0xa3, 0xaf));
        cmt.setFontItalic(true);
        m_rules.append({QRegularExpression(QStringLiteral("--[^\\n]*")), cmt});
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const Rule &r : m_rules)
        {
            auto it = r.re.globalMatch(text);
            while (it.hasNext())
            {
                const QRegularExpressionMatch m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.fmt);
            }
        }
    }

private:
    struct Rule
    {
        QRegularExpression re;
        QTextCharFormat fmt;
    };
    QVector<Rule> m_rules;
};

// ============================================================================
// PythonHighlighter：.py 文档高亮（M2.6，与 LuaHighlighter 同构同配色）
// ============================================================================
class PythonHighlighter final : public QSyntaxHighlighter
{
public:
    explicit PythonHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
    {
        // 关键字（紫）
        QTextCharFormat kw;
        kw.setForeground(QColor(0x7c, 0x3a, 0xed));
        kw.setFontWeight(QFont::Bold);
        m_rules.append({QRegularExpression(QStringLiteral(
                            "\\b(def|class|return|if|elif|else|for|while|in|break|continue|"
                            "import|from|as|with|try|except|finally|raise|lambda|yield|"
                            "global|nonlocal|pass|assert|del|None|True|False|and|or|not|"
                            "is|async|await)\\b")),
                        kw});
        // 字符串（绿）
        QTextCharFormat str;
        str.setForeground(QColor(0x05, 0x96, 0x69));
        m_rules.append({QRegularExpression(QStringLiteral(
                            "(?:[frb]{0,2})\"(?:[^\"\\\\]|\\\\.)*\"|(?:[frb]{0,2})'(?:[^'\\\\]|\\\\.)*'")),
                        str});
        // 数字（橙）
        QTextCharFormat num;
        num.setForeground(QColor(0xd9, 0x77, 0x06));
        m_rules.append({QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")), num});
        // 全局对象（蓝粗）：ds（数据集视图）/ np（预注入 numpy）
        QTextCharFormat glob;
        glob.setForeground(QColor(0x25, 0x63, 0xeb));
        glob.setFontWeight(QFont::Bold);
        m_rules.append({QRegularExpression(QStringLiteral("\\b(ds|np|self)\\b")), glob});
        // 注释（灰斜体，最后套用保证覆盖）
        QTextCharFormat cmt;
        cmt.setForeground(QColor(0x9c, 0xa3, 0xaf));
        cmt.setFontItalic(true);
        m_rules.append({QRegularExpression(QStringLiteral("#[^\\n]*")), cmt});
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const Rule &r : m_rules)
        {
            auto it = r.re.globalMatch(text);
            while (it.hasNext())
            {
                const QRegularExpressionMatch m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.fmt);
            }
        }
    }

private:
    struct Rule
    {
        QRegularExpression re;
        QTextCharFormat fmt;
    };
    QVector<Rule> m_rules;
};

// ============================================================================
// ScriptCodeEditor：编辑器薄壳（gutter = 行号 + 断点圆点 + 错误行标记）
// ============================================================================
namespace
{
class GutterWidget : public QWidget
{
public:
    explicit GutterWidget(ScriptCodeEditor *editor) : QWidget(editor), m_editor(editor)
    {
        setObjectName(QStringLiteral("scriptEditorGutter"));
        installEventFilter(editor); // 点击转交编辑器（断点切换）
    }

protected:
    void paintEvent(QPaintEvent *ev) override { m_editor->paintGutter(ev); }

private:
    ScriptCodeEditor *m_editor;
};
} // namespace

ScriptCodeEditor::ScriptCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_gutter(new GutterWidget(this))
{
    setFont(QFont(QStringLiteral("monospace"), 10));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    updateGutterWidth();
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this](int) { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, int dy) {
                if (dy)
                    m_gutter->scroll(0, dy);
                else
                    m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        m_gutter->update(); // 当前行箭头
        refreshExtraSelections();
    });
    // 当前行/错误行高亮走 extraSelections。
}

ScriptCodeEditor::~ScriptCodeEditor() = default;

int ScriptCodeEditor::gutterWidth() const
{
    int digits = 2;
    int max = qMax(1, blockCount());
    while (max >= 100)
    {
        max /= 10;
        ++digits;
    }
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void ScriptCodeEditor::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void ScriptCodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(cr.left(), cr.top(), gutterWidth(), cr.height());
}

bool ScriptCodeEditor::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_gutter && ev->type() == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(ev);
        const int line = static_cast<int>(
            cursorForPosition(QPoint(0, me->position().y())).blockNumber()) + 1;
        if (m_breakpoints.contains(line))
            m_breakpoints.remove(line);
        else
            m_breakpoints.insert(line);
        if (onBreakpointToggle)
            onBreakpointToggle(line);
        m_gutter->update();
        return true;
    }
    return QPlainTextEdit::eventFilter(obj, ev);
}

void ScriptCodeEditor::paintGutter(QPaintEvent *ev)
{
    QPainter p(m_gutter);
    p.fillRect(ev->rect(), QColor(0xf8, 0xfa, 0xfc));
    p.setPen(QColor(0x9c, 0xa3, 0xaf));
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int curLine = textCursor().blockNumber() + 1;
    while (block.isValid() && top <= ev->rect().bottom())
    {
        if (block.isVisible() && bottom >= ev->rect().top())
        {
            const int line = block.blockNumber() + 1;
            // 断点圆点（红）
            if (m_breakpoints.contains(line))
            {
                p.setBrush(QColor(0xdc, 0x26, 0x26));
                p.setPen(Qt::NoPen);
                p.drawEllipse(3, top + 2, 8, 8);
                p.setPen(QColor(0x6b, 0x72, 0x80));
            }
            // 当前行箭头（蓝）
            if (line == curLine)
            {
                p.setPen(QColor(0x25, 0x63, 0xeb));
                p.drawText(2, top, gutterWidth() - 16, fontMetrics().height(),
                           Qt::AlignRight, QStringLiteral("▶"));
            }
            p.setPen(QColor(line == curLine ? 0x1f2937 : 0x9ca3af));
            p.drawText(14, top, gutterWidth() - 18, fontMetrics().height(),
                       Qt::AlignRight, QString::number(line));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
    // 右边框
    p.setPen(QColor(0xe5, 0xe7, 0xeb));
    p.drawLine(ev->rect().topRight(), ev->rect().bottomRight());
}

void ScriptCodeEditor::setErrorLine(int line)
{
    m_errorLine = line;
    refreshExtraSelections();
    if (line > 0)
        jumpToLine(line);
}

void ScriptCodeEditor::setDebugLine(int line)
{
    m_debugLine = line;
    refreshExtraSelections();
    if (line > 0)
        jumpToLine(line);
}

void ScriptCodeEditor::jumpToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    QTextCursor c = textCursor();
    c.setPosition(block.position());
    setTextCursor(c);
    centerCursor();
    setFocus();
}

void ScriptCodeEditor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> sels;
    // 当前行
    QTextEdit::ExtraSelection cur;
    cur.format.setBackground(QColor(0xfd, 0xf6, 0xe3));
    cur.format.setProperty(QTextFormat::FullWidthSelection, true);
    cur.cursor = textCursor();
    cur.cursor.clearSelection();
    sels.append(cur);
    // 错误行（红）
    if (m_errorLine > 0)
    {
        const QTextBlock b = document()->findBlockByNumber(m_errorLine - 1);
        if (b.isValid())
        {
            QTextEdit::ExtraSelection err;
            err.format.setBackground(QColor(0xfe, 0xe2, 0xe2));
            err.format.setProperty(QTextFormat::FullWidthSelection, true);
            err.cursor = QTextCursor(b);
            sels.append(err);
        }
    }
    // 调试暂停行（黄）
    if (m_debugLine > 0)
    {
        const QTextBlock b = document()->findBlockByNumber(m_debugLine - 1);
        if (b.isValid())
        {
            QTextEdit::ExtraSelection dbg;
            dbg.format.setBackground(QColor(0xfe, 0xf3, 0xc7));
            dbg.format.setProperty(QTextFormat::FullWidthSelection, true);
            dbg.cursor = QTextCursor(b);
            sels.append(dbg);
        }
    }
    setExtraSelections(sels);
}

void ScriptCodeEditor::keyPressEvent(QKeyEvent *ev)
{
    // Ctrl+Tab 交给面板级 QShortcut（切标签）。
    if (ev->modifiers() & Qt::ControlModifier && ev->key() == Qt::Key_Tab)
    {
        ev->ignore();
        return;
    }
    QPlainTextEdit::keyPressEvent(ev);
}

// ============================================================================
// ScriptEditorPanel
// ============================================================================
ScriptEditorPanel::ScriptEditorPanel(AppContext &appCtx, IIntentExecutor &executor,
                                     QWidget *parent)
    : QWidget(parent), m_app(&appCtx), m_executor(&executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    // 调试/运行工具条（高频微操作；粗粒度命令在全局 Ribbon）
    auto *bar = new QHBoxLayout();
    auto addBtn = [this, bar](const QString &text, const QString &tip, auto slot) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, slot);
        bar->addWidget(b);
        return b;
    };
    addBtn(QStringLiteral("▶ 运行"), QStringLiteral("运行当前标签（F5）"),
           [this]() { onRunCurrent(false); });
    addBtn(QStringLiteral("🐞 调试"), QStringLiteral("调试运行当前标签：入口暂停、断点、单步（F7）"),
           [this]() { onRunCurrent(true); });
    // 调试控制：经模块通道恢复/单步（引擎归 ScriptModule——层契约）
    addBtn(QStringLiteral("⏭ 继续"), QStringLiteral("继续执行到下一断点（F9）"),
           [this]() { dispatchDebugCmd(QStringLiteral("continue")); });
    addBtn(QStringLiteral("⏭ 跳过"), QStringLiteral("单步跳过：函数体整段跨过（F10）"),
           [this]() { dispatchDebugCmd(QStringLiteral("stepover")); });
    addBtn(QStringLiteral("⤵ 进入"), QStringLiteral("单步进入：跟进被调函数（F11）"),
           [this]() { dispatchDebugCmd(QStringLiteral("stepin")); });
    addBtn(QStringLiteral("⤴ 跳出"), QStringLiteral("单步跳出：跑完当前函数（Shift+F11）"),
           [this]() { dispatchDebugCmd(QStringLiteral("stepout")); });
    addBtn(QStringLiteral("■ 停止"), QStringLiteral("停止执行/调试"),
           [this]() { onStop(); });
    addBtn(QStringLiteral("💾 保存"), QStringLiteral("保存当前标签（Ctrl+S）"),
           [this]() { saveCurrentDocument(); });
    bar->addStretch(1);
    lay->addLayout(bar);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            &ScriptEditorPanel::onTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged, this, &ScriptEditorPanel::onTabChanged);
    // 空态提示
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #e5e7eb; }"));
    lay->addWidget(m_tabs, 1);

    auto *empty = new QLabel(QStringLiteral(
        "双击左侧脚本库打开脚本，或“新建”开始编写。\nF5 运行 · F7 调试 · Ctrl+S 保存"),
        this);
    empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet(QStringLiteral("color: #9ca3af;"));
    // 空标签页占位：加一个只读编辑器提示页
    auto *ph = new ScriptCodeEditor(this);
    ph->setReadOnly(true);
    ph->setPlainText(QStringLiteral("-- PlotScope 脚本工作台\n"
                                    "-- 双击左侧脚本库打开脚本，或新建一个。\n\n"
                                    "-- 示例：\n"
                                    "-- local v = ctx:column(\"vz\")\n"
                                    "-- app.call(\"plot.add\", { series = v })\n"));
    auto *hl = new LuaHighlighter(ph->document());
    ph->highlighter = hl;
    m_tabs->addTab(ph, QStringLiteral("欢迎"));
    Q_UNUSED(empty);

    // 快捷键（面板级）
    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, [this]() { onRunCurrent(false); });
    auto *f7 = new QShortcut(QKeySequence(Qt::Key_F7), this);
    connect(f7, &QShortcut::activated, this, [this]() { onRunCurrent(true); });
    // 调试控制快捷键（与工具条同路径：模块通道）
    const struct { Qt::Key key; Qt::KeyboardModifiers mods; const char *cmd; } kDbgKeys[] = {
        {Qt::Key_F9,  Qt::NoModifier,      "continue"},
        {Qt::Key_F10, Qt::NoModifier,      "stepover"},
        {Qt::Key_F11, Qt::NoModifier,      "stepin"},
        {Qt::Key_F11, Qt::ShiftModifier,   "stepout"},
    };
    for (const auto &k : kDbgKeys)
    {
        auto *sc = new QShortcut(QKeySequence(static_cast<int>(k.key) | static_cast<int>(k.mods)), this);
        const QString cmd = QString::fromLatin1(k.cmd);
        connect(sc, &QShortcut::activated, this, [this, cmd]() { dispatchDebugCmd(cmd); });
    }
    auto *save = new QShortcut(QKeySequence::Save, this);
    connect(save, &QShortcut::activated, this, [this]() { saveCurrentDocument(); });
    auto *ctrlW = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(ctrlW, &QShortcut::activated, this, [this]() {
        const int i = m_tabs->currentIndex();
        if (i >= 0)
            onTabCloseRequested(i);
    });
    auto *ctrlTab = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(ctrlTab, &QShortcut::activated, this, [this]() {
        const int n = m_tabs->count();
        if (n > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % n);
    });

    attachViewActions();
}

ScriptEditorPanel::~ScriptEditorPanel() = default;

void ScriptEditorPanel::attachViewActions()
{
    InteractionRegistry &reg = m_app->interactions();
    QPointer<ScriptEditorPanel> guard(this);

    reg.registerViewAction(kActOpen,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        const QString name = params.value(QStringLiteral("name")).toString();
        return guard->openScript(name) ? Result<QVariant>(QVariant(true))
                                       : fail(err::NoEnt, QStringLiteral("脚本不存在: %1").arg(name));
    });
    reg.registerViewAction(kActNew,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        const QString name = params.value(QStringLiteral("name")).toString();
        const QString text = params.value(QStringLiteral("text")).toString();
        return guard->newDocument(name, text)
                   ? Result<QVariant>(QVariant(true))
                   : fail(err::BadName, QStringLiteral("无法新建: %1").arg(name));
    });
    reg.registerViewAction(kActClosed,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (guard)
            guard->closeDocumentByName(params.value(QStringLiteral("name")).toString());
        return Result<QVariant>(QVariant(true));
    });
    reg.registerViewAction(kActGoto,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        guard->gotoLine(params.value(QStringLiteral("name")).toString(),
                        params.value(QStringLiteral("line")).toInt());
        return Result<QVariant>(QVariant(true));
    });
    reg.registerViewAction(kActInsert,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        guard->insertText(params.value(QStringLiteral("text")).toString());
        return Result<QVariant>(QVariant(true));
    });
    // script.editor.run {debug?}：执行当前标签（Ribbon"运行/调试运行"与 F5/F7 同路径）
    reg.registerViewAction(kActEditorRun,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        guard->onRunCurrent(params.value(QStringLiteral("debug")).toBool());
        return Result<QVariant>(QVariant(true));
    });
    // script.debug.pause：调试暂停 → 当前行高亮跳转（编辑器侧，与调试面板的 update 分开注册）
    reg.registerViewAction(kActDebugPause,
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        const QString name = params.value(QStringLiteral("name")).toString();
        const int line = params.value(QStringLiteral("line")).toInt();
        Document *d = (name.isEmpty() || guard->docByName(name) == nullptr)
                          ? guard->currentDoc() : guard->docByName(name);
        if (d && line > 0)
            d->editor->setDebugLine(line);
        return Result<QVariant>(QVariant(true));
    });
    // script.debug.done：调试结束 → 解除暂停高亮
    reg.registerViewAction(kActDebugDone,
                           [guard](const QVariantMap &) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("编辑器面板已关闭"));
        for (const Document &d : guard->m_docs)
            d.editor->setDebugLine(-1);
        return Result<QVariant>(QVariant(true));
    });
}

int ScriptEditorPanel::documentCount() const
{
    // 排除“欢迎”占位页（index 0 由构造建立）
    return qMax(0, m_tabs->count() - 1);
}

QString ScriptEditorPanel::currentDocumentName() const
{
    auto *ed = static_cast<ScriptCodeEditor *>(m_tabs->currentWidget());
    if (!ed)
        return {};
    for (const Document &d : m_docs)
        if (d.editor == ed)
            return d.name;
    return {};
}

ScriptEditorPanel::Document *ScriptEditorPanel::currentDoc()
{
    auto *ed = static_cast<ScriptCodeEditor *>(m_tabs->currentWidget());
    if (!ed)
        return nullptr;
    for (Document &d : m_docs)
        if (d.editor == ed)
            return &d;
    return nullptr;
}

ScriptEditorPanel::Document *ScriptEditorPanel::docAt(int index)
{
    auto *ed = static_cast<ScriptCodeEditor *>(m_tabs->widget(index));
    if (!ed)
        return nullptr;
    for (Document &d : m_docs)
        if (d.editor == ed)
            return &d;
    return nullptr;
}

ScriptEditorPanel::Document *ScriptEditorPanel::docByName(const QString &name)
{
    for (Document &d : m_docs)
        if (d.name == name)
            return &d;
    return nullptr;
}

int ScriptEditorPanel::openDocument(Document doc, const QString &title)
{
    m_docs.append(doc);
    const int idx = m_tabs->addTab(doc.editor, title);
    m_tabs->setCurrentIndex(idx);
    return idx;
}

void ScriptEditorPanel::updateTabTitle(int index)
{
    if (const Document *d = docAt(index))
        m_tabs->setTabText(index, d->name.isEmpty() ? QStringLiteral("untitled")
                                                     : (d->dirty ? d->name + QStringLiteral(" ●") : d->name));
}

void ScriptEditorPanel::markDirty(Document *doc, bool dirty)
{
    if (!doc || doc->dirty == dirty)
        return;
    doc->dirty = dirty;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i) == doc->editor)
            updateTabTitle(i);
}

void ScriptEditorPanel::onEditorModified()
{
    if (Document *d = currentDoc())
        markDirty(d, true);
}

void ScriptEditorPanel::onTabChanged(int index)
{
    Q_UNUSED(index);
}

bool ScriptEditorPanel::openScript(const QString &name)
{
    if (name.isEmpty())
        return false;
    if (Document *existing = docByName(name))
    {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (m_tabs->widget(i) == existing->editor)
                m_tabs->setCurrentIndex(i);
        return true;
    }
    const auto &ops = m_app->interactions().transformScriptOps();
    if (!ops.valid() || !ops.list().contains(name))
        return false;
    const QByteArray source = ops.resolve(name, QString());
    auto *ed = new ScriptCodeEditor(this);
    ed->setPlainText(QString::fromUtf8(source));
    ed->highlighter = new LuaHighlighter(ed->document());
    ed->onBreakpointToggle = [this](int) { /* 断点集在编辑器上，调试时直接读 */ };
    connect(ed, &QPlainTextEdit::textChanged, this, &ScriptEditorPanel::onEditorModified);
    Document d;
    d.editor = ed;
    d.name = name;
    d.dirty = false;
    openDocument(std::move(d), name);
    return true;
}

bool ScriptEditorPanel::newDocument(const QString &name, const QString &initialText)
{
    if (!validScriptName(name) || docByName(name))
        return false;
    auto *ed = new ScriptCodeEditor(this);
    if (isPythonDoc(name))
    {
        // .py 文档：Python 模板 + Python 高亮；运行走 script.py.exec（子进程桥）。
        ed->setPlainText(initialText.isEmpty()
                             ? QStringLiteral("# %1\n"
                                             "# ds['col'] 读数据集列（ndarray）；ds['新列'] = ... 写回产生 v2。\n"
                                             "import numpy as np\n\n")
                                   .arg(name.chopped(3))
                             : initialText);
        ed->highlighter = new PythonHighlighter(ed->document());
    }
    else
    {
        ed->setPlainText(initialText.isEmpty() ? QStringLiteral("-- %1.lua\n\n").arg(name)
                                               : initialText);
        ed->highlighter = new LuaHighlighter(ed->document());
    }
    connect(ed, &QPlainTextEdit::textChanged, this, &ScriptEditorPanel::onEditorModified);
    Document d;
    d.editor = ed;
    d.name = name;
    d.dirty = true;
    openDocument(std::move(d), name + QStringLiteral(" ●"));
    return true;
}

bool ScriptEditorPanel::closeDocumentByName(const QString &name)
{
    for (int i = 0; i < m_tabs->count(); ++i)
    {
        if (Document *d = docAt(i); d && d->name == name)
        {
            m_docs.removeAt(i);
            QWidget *w = m_tabs->widget(i);
            m_tabs->removeTab(i);
            delete w;
            return true;
        }
    }
    return false;
}

bool ScriptEditorPanel::ensureSavedName(Document *doc)
{
    if (!doc->name.isEmpty())
        return true;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("保存脚本"),
                                               QStringLiteral("脚本名（小写字母/数字/_/-；.py 结尾走 Python 引擎）："),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || !validScriptName(name))
    {
        appendConsole(QStringLiteral("✘ 无效脚本名"), true);
        return false;
    }
    if (docByName(name))
    {
        appendConsole(QStringLiteral("✘ 同名脚本已打开"), true);
        return false;
    }
    doc->name = name;
    return true;
}

bool ScriptEditorPanel::saveCurrentDocument()
{
    Document *doc = currentDoc();
    if (!doc)
        return false;
    if (isPythonDoc(doc->name))
    {
        // v1：.py 是工作台草稿（脚本库只存 Lua）；运行即所得。
        appendConsole(QStringLiteral("ℹ .py 脚本暂不入库（运行即可，保存能力随脚本库多语言扩展）"), false);
        return false;
    }
    if (!ensureSavedName(doc))
        return false;
    const auto &ops = m_app->interactions().transformScriptOps();
    if (!ops.valid())
    {
        appendConsole(QStringLiteral("✘ 脚本库不可用（script 模块未加载）"), true);
        return false;
    }
    const Result<nextpilot::plotscope::uischema::TransformDescriptor> r =
        ops.save(doc->name, doc->editor->toPlainText().toUtf8());
    if (!r)
    {
        appendConsole(QStringLiteral("✘ 保存失败: %1").arg(r.error().message), true);
        return false;
    }
    doc->dirty = false;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i) == doc->editor)
            updateTabTitle(i);
    appendConsole(QStringLiteral("✔ 已保存 %1.lua").arg(doc->name), false);
    return true;
}

void ScriptEditorPanel::gotoLine(const QString &name, int line)
{
    Document *d = (name.isEmpty() || name == QLatin1String("repl")) ? currentDoc() : docByName(name);
    if (!d)
        return;
    d->editor->setErrorLine(line);
}

void ScriptEditorPanel::insertText(const QString &text)
{
    if (Document *d = currentDoc())
        d->editor->insertPlainText(text);
}

void ScriptEditorPanel::onTabCloseRequested(int index)
{
    Document *d = docAt(index);
    if (!d)
        return;
    if (d->dirty)
    {
        const auto ret = QMessageBox::question(
            this, QStringLiteral("未保存"),
            QStringLiteral("%1 有未保存的修改，关闭前保存？")
                .arg(d->name.isEmpty() ? QStringLiteral("untitled") : d->name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel)
            return;
        if (ret == QMessageBox::Save)
        {
            // 临时切到该标签再保存
            m_tabs->setCurrentIndex(index);
            if (!saveCurrentDocument())
                return;
        }
    }
    m_docs.removeAt(index);
    QWidget *w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete w;
}

void ScriptEditorPanel::appendConsole(const QString &text, bool isError)
{
    QVariantMap p;
    p.insert(QStringLiteral("text"), text);
    p.insert(QStringLiteral("error"), isError);
    Result<QVariant> out{QVariant()};
    m_app->interactions().dispatchViewAction(kActConsoleAppend, p, out);
}

void ScriptEditorPanel::reportErrorLine(const QString &message)
{
    QString name;
    int line = 0;
    if (parseErrorLocation(message, &name, &line))
        gotoLine(name, line);
}

void ScriptEditorPanel::onRunCurrent(bool debugMode)
{
    Document *doc = currentDoc();
    if (!doc)
    {
        appendConsole(QStringLiteral("✘ 没有打开的脚本"), true);
        return;
    }
    doc->editor->setErrorLine(-1);
    const QString title = doc->name.isEmpty() ? QStringLiteral("untitled") : doc->name;

    if (debugMode)
    {
        if (isPythonDoc(doc->name))
        {
            appendConsole(QStringLiteral("ℹ .py 调试（debugpy）远期接入，请用普通运行"), false);
            return;
        }
        // 调试运行：经模块通道（script.workbench.debug），带断点集合。
        // 执行在暂停期嵌套等恢复（UI 保持响应），返回即运行结束。
        appendConsole(QStringLiteral("🐞 调试 %1 ...").arg(title), false);
        m_debugging = true;
        QVariantList bp;
        for (int line : doc->editor->breakpoints())
            bp.append(line);
        QVariantMap p;
        p.insert(QStringLiteral("source"), doc->editor->toPlainText().toUtf8());
        p.insert(QStringLiteral("name"), title);
        p.insert(QStringLiteral("breakpoints"), bp);
        Result<QVariant> out{QVariant()};
        const Result<QVariant> r = m_app->interactions().dispatchViewAction(kActDebugRun, p, out)
                                       ? Result<QVariant>(out)
                                       : Result<QVariant>(fail(err::NoEnt, QStringLiteral("脚本模块未就绪")));
        m_debugging = false;
        if (!r)
            appendConsole(QStringLiteral("✘ %1").arg(r.error().message), true);
        else
            appendConsole(QStringLiteral("✔ 调试完成"), false);
        return;
    }

    appendConsole(QStringLiteral("▶ 运行 %1 ...").arg(title), false);

    // 执行走模块通道（引擎归 ScriptModule，GUI 层不依赖 script 内部类——层契约）。
    QVariantMap p;
    p.insert(QStringLiteral("source"), doc->editor->toPlainText().toUtf8());
    p.insert(QStringLiteral("name"), title);
    Result<QVariant> out{QVariant()};
    const Result<QVariant> r = m_app->interactions().dispatchViewAction(kActRun, p, out)
                                   ? Result<QVariant>(out)
                                   : Result<QVariant>(fail(err::NoEnt, QStringLiteral("脚本模块未就绪")));
    if (!r)
    {
        appendConsole(QStringLiteral("✘ %1").arg(r.error().message), true);
        reportErrorLine(r.error().message);
    }
    else
    {
        appendConsole(QStringLiteral("✔ 完成"), false);
    }
    Result<QVariant> clearOut{QVariant()};
    m_app->interactions().dispatchViewAction(kActDebugClear, QVariantMap(), clearOut);
    m_debugging = false;
}

void ScriptEditorPanel::dispatchDebugCmd(const QString &cmd)
{
    QVariantMap p;
    Result<QVariant> out{QVariant()};
    if (!m_app->interactions().dispatchViewAction(kActDebugCmd + cmd, p, out))
        appendConsole(QStringLiteral("✘ 脚本模块未就绪"), true);
    else if (!out)
        appendConsole(QStringLiteral("⚠ %1").arg(out.error().message), true);
}

void ScriptEditorPanel::onStop()
{
    // 调试会话：经模块通道停止（暂停期立即中断；未暂停时置位待下次暂停生效）。
    // 普通运行仍不能强杀 VM（Lua 无安全中止，v1 不做）。
    QVariantMap p;
    Result<QVariant> out{QVariant()};
    if (m_app->interactions().dispatchViewAction(kActDebugCmd + QStringLiteral("stop"), p, out) && out)
    {
        appendConsole(QStringLiteral("■ 停止请求已发出"), false);
        return;
    }
    m_stopRequested = true;
    appendConsole(QStringLiteral("■ 停止请求已发出（普通运行不可强停，调试暂停期才可中断）"), false);
}

// ============================================================================
// ScriptConsolePanel
// ============================================================================
ScriptConsolePanel::ScriptConsolePanel(AppContext &appCtx, IIntentExecutor &executor,
                                       QWidget *parent)
    : QWidget(parent), m_app(&appCtx), m_executor(&executor)
{
    Q_UNUSED(m_executor); // 求值走模块通道；executor 仅满足工厂统一签名
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 2);

    m_out = new QPlainTextEdit(this);
    m_out->setReadOnly(true);
    m_out->setFont(QFont(QStringLiteral("monospace"), 10));
    m_out->setMaximumBlockCount(5000);
    m_out->viewport()->installEventFilter(this);
    m_out->setPlainText(QStringLiteral("-- REPL：输入 Lua 表达式回车求值；print/app.log 输出在这里。\n"
                                       "-- 例：2^10   |   app.call(\"data.datasets\")\n"));
    lay->addWidget(m_out, 1);

    m_input = new QLineEdit(this);
    m_input->setFont(QFont(QStringLiteral("monospace"), 10));
    m_input->setPlaceholderText(QStringLiteral(">> 输入 Lua（回车求值）"));
    connect(m_input, &QLineEdit::returnPressed, this, &ScriptConsolePanel::onSubmit);
    lay->addWidget(m_input);

    // REPL 引擎不在面板：求值经 script.workbench.repl 发给 ScriptModule
    // （持久 VM 归模块持有——层契约：ng_module_script_gui 禁止横向依赖 ng_module_script）；
    // print/app.log 输出经 script.console.append 回显（ScriptModule 的 sink 发到本面板）。

    // console.append：供 ScriptModule（运行输出/REPL 打印）与 editor 面板回显
    QPointer<ScriptConsolePanel> guard(this);
    m_app->interactions().registerViewAction(
        kActConsoleAppend, [guard](const QVariantMap &params) -> Result<QVariant> {
            if (!guard)
                return fail(err::NoEnt, QStringLiteral("控制台已关闭"));
            guard->append(params.value(QStringLiteral("text")).toString(),
                          params.value(QStringLiteral("error")).toBool());
            return Result<QVariant>(QVariant(true));
        });
}

ScriptConsolePanel::~ScriptConsolePanel() = default;

void ScriptConsolePanel::append(const QString &text, bool isError)
{
    if (isError)
        m_out->appendHtml(QStringLiteral("<span style=\"color:#dc2626\">%1</span>")
                              .arg(text.toHtmlEscaped()));
    else
        m_out->appendPlainText(text);
}

void ScriptConsolePanel::onSubmit()
{
    const QString src = m_input->text();
    if (src.trimmed().isEmpty())
        return;
    m_input->clear();
    append(QStringLiteral(">> %1").arg(src), false);
    // 求值经模块通道（script.workbench.repl）；print 输出已由模块 sink 回显。
    QVariantMap p;
    p.insert(QStringLiteral("code"), src);
    Result<QVariant> out{QVariant()};
    const Result<QVariant> r = m_app->interactions().dispatchViewAction(kActRepl, p, out)
                                   ? Result<QVariant>(out)
                                   : Result<QVariant>(fail(err::NoEnt, QStringLiteral("脚本模块未就绪")));
    if (!r)
    {
        append(QStringLiteral("✘ %1").arg(r.error().message), true);
        return;
    }
    if (!r.value().isNull() && r.value().isValid())
        append(QStringLiteral("→ %1").arg(variantToDisplay(r.value())), false);
}

bool ScriptConsolePanel::eventFilter(QObject *obj, QEvent *ev)
{
    // 点击输出里的错误行 → 提取 name:line 跳编辑器
    if (obj == m_out->viewport() && ev->type() == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(ev);
        QTextCursor c = m_out->cursorForPosition(me->position().toPoint());
        const QString text = c.block().text();
        QString name;
        int line = 0;
        if (parseErrorLocation(text, &name, &line))
        {
            QVariantMap p;
            p.insert(QStringLiteral("name"), name);
            p.insert(QStringLiteral("line"), line);
            Result<QVariant> out{QVariant()};
            m_app->interactions().dispatchViewAction(kActGoto, p, out);
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// ============================================================================
// ScriptFilesPanel
// ============================================================================
ScriptFilesPanel::ScriptFilesPanel(AppContext &appCtx, IIntentExecutor &executor,
                                   QWidget *parent)
    : QWidget(parent), m_app(&appCtx)
{
    Q_UNUSED(executor);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto *bar = new QHBoxLayout();
    auto addBtn = [&bar, this](const QString &text, const QString &tip, auto slot) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, slot);
        bar->addWidget(b);
    };
    addBtn(QStringLiteral("＋新建"), QStringLiteral("新建脚本"), []() {});
    // 上面 lambda 占位，下面重新连接到成员（Qt 连接成员函数更直观）
    for (int i = 0; i < bar->count(); ++i)
        bar->takeAt(0); // 清空重来，简单直接
    auto *newBtn = new QToolButton(this);
    newBtn->setText(QStringLiteral("＋新建"));
    newBtn->setToolTip(QStringLiteral("新建脚本"));
    newBtn->setAutoRaise(true);
    connect(newBtn, &QToolButton::clicked, this, &ScriptFilesPanel::onNew);
    auto *delBtn = new QToolButton(this);
    delBtn->setText(QStringLiteral("✕删除"));
    delBtn->setToolTip(QStringLiteral("删除选中脚本"));
    delBtn->setAutoRaise(true);
    connect(delBtn, &QToolButton::clicked, this, &ScriptFilesPanel::onDelete);
    auto *refBtn = new QToolButton(this);
    refBtn->setText(QStringLiteral("↻刷新"));
    refBtn->setToolTip(QStringLiteral("重新扫描脚本库"));
    refBtn->setAutoRaise(true);
    connect(refBtn, &QToolButton::clicked, this, &ScriptFilesPanel::onRefresh);
    bar->addWidget(newBtn);
    bar->addWidget(delBtn);
    bar->addWidget(refBtn);
    bar->addStretch(1);
    lay->addLayout(bar);

    auto *label = new QLabel(QStringLiteral("脚本库"), this);
    label->setStyleSheet(QStringLiteral("color:#6b7280; font-weight:bold;"));
    lay->addWidget(label);

    m_list = new QListWidget(this);
    m_list->setFont(QFont(QStringLiteral("monospace"), 10));
    connect(m_list, &QListWidget::itemActivated, this, &ScriptFilesPanel::onOpen);
    lay->addWidget(m_list, 1);

    onRefresh();
}

void ScriptFilesPanel::onRefresh()
{
    m_list->clear();
    const auto &ops = m_app->interactions().transformScriptOps();
    if (!ops.valid())
    {
        auto *it = new QListWidgetItem(QStringLiteral("(脚本库不可用：script 模块未加载)"),
                                       m_list);
        it->setFlags(Qt::NoItemFlags);
        return;
    }
    const QStringList names = ops.list();
    for (const QString &n : names)
        m_list->addItem(n + QStringLiteral(".lua"));
    if (names.isEmpty())
    {
        auto *it = new QListWidgetItem(QStringLiteral("(空——新建一个脚本吧)"), m_list);
        it->setFlags(Qt::NoItemFlags);
    }
}

void ScriptFilesPanel::onOpen(QListWidgetItem *item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const QString name = item->text().chopped(4); // 去掉 ".lua"
    QVariantMap p;
    p.insert(QStringLiteral("name"), name);
    Result<QVariant> out{QVariant()};
    // 走模块通道：编辑器按需创建（首次打开自动进中央区页签），再转发 script.open
    if (!m_app->interactions().dispatchViewAction(QStringLiteral("script.workbench.open"), p, out))
    {
        if (out.ok())
            QMessageBox::warning(this, QStringLiteral("打开失败"),
                                 QStringLiteral("脚本不存在: %1").arg(name));
    }
}

void ScriptFilesPanel::onNew()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("新建脚本"),
        QStringLiteral("脚本名（小写字母/数字/_/-；.py 结尾走 Python 引擎）："),
        QLineEdit::Normal, QStringLiteral("myscript"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QVariantMap p;
    p.insert(QStringLiteral("name"), name.trimmed());
    Result<QVariant> out{QVariant()};
    // 走模块通道：编辑器按需创建，再转发 script.new
    if (!m_app->interactions().dispatchViewAction(QStringLiteral("script.workbench.new"), p, out))
        QMessageBox::warning(this, QStringLiteral("新建失败"), out.error().message);
}

void ScriptFilesPanel::onDelete()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const QString name = item->text().chopped(4);
    const auto ret = QMessageBox::question(this, QStringLiteral("删除脚本"),
                                           QStringLiteral("确定删除 %1.lua ？").arg(name));
    if (ret != QMessageBox::Yes)
        return;
    const auto &ops = m_app->interactions().transformScriptOps();
    if (!ops.valid())
        return;
    const Result<void> r = ops.remove(name);
    if (!r)
    {
        QMessageBox::warning(this, QStringLiteral("删除失败"), r.error().message);
        return;
    }
    QVariantMap p;
    p.insert(QStringLiteral("name"), name);
    Result<QVariant> out{QVariant()};
    m_app->interactions().dispatchViewAction(kActClosed, p, out);
    onRefresh();
}

// ============================================================================
// ScriptDebugPanel
// ============================================================================
ScriptDebugPanel::ScriptDebugPanel(AppContext &appCtx, IIntentExecutor &executor,
                                   QWidget *parent)
    : QWidget(parent), m_app(&appCtx)
{
    Q_UNUSED(executor);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    lay->addWidget(m_tabs);

    // 页签 1：调试（调用栈 + 变量）
    m_debugTree = new QTreeWidget(this);
    m_debugTree->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("值")});
    m_debugTree->setFont(QFont(QStringLiteral("monospace"), 10));
    m_tabs->addTab(m_debugTree, QStringLiteral("调试"));

    // 页签 2：API 速查（意图层 op 目录实时导出）
    m_apiList = new QListWidget(this);
    m_apiList->setFont(QFont(QStringLiteral("monospace"), 10));
    m_apiList->setToolTip(QStringLiteral("双击插入 app.call 调用模板到当前编辑器"));
    connect(m_apiList, &QListWidget::itemActivated, this, &ScriptDebugPanel::onInsertApi);
    m_tabs->addTab(m_apiList, QStringLiteral("API 速查"));

    auto *hint = new QLabel(QStringLiteral("调试暂停时显示变量与调用栈；速查双击插入模板。"), this);
    hint->setStyleSheet(QStringLiteral("color:#9ca3af;"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // api 列表
    const QStringList ops = m_app->interactions().opCatalogue();
    for (const QString &op : ops)
        m_apiList->addItem(op);

    // script.debug.update / clear
    QPointer<ScriptDebugPanel> guard(this);
    m_app->interactions().registerViewAction(
        kActDebugUpdate, [guard](const QVariantMap &params) -> Result<QVariant> {
            if (guard)
                guard->applyUpdate(params);
            return Result<QVariant>(QVariant(true));
        });
    m_app->interactions().registerViewAction(
        kActDebugClear, [guard](const QVariantMap &) -> Result<QVariant> {
            if (guard)
                guard->clearPause();
            return Result<QVariant>(QVariant(true));
        });
}

void ScriptDebugPanel::applyUpdate(const QVariantMap &params)
{
    m_debugTree->clear();
    const bool paused = params.value(QStringLiteral("paused")).toBool();
    if (!paused)
        return;
    // 暂停位置
    auto *pos = new QTreeWidgetItem(m_debugTree, {QStringLiteral("⏸ 暂停"),
        QStringLiteral("%1:%2").arg(params.value(QStringLiteral("name")).toString(),
                                    QString::number(params.value(QStringLiteral("line")).toInt()))});
    pos->setFirstColumnSpanned(true);
    // 调用栈
    auto *stackRoot = new QTreeWidgetItem(m_debugTree, {QStringLiteral("调用栈"), QString()});
    for (const QVariant &frame : params.value(QStringLiteral("stack")).toList())
        new QTreeWidgetItem(stackRoot, {frame.toString(), QString()});
    stackRoot->setExpanded(true);
    // 局部变量
    auto *varRoot = new QTreeWidgetItem(m_debugTree, {QStringLiteral("变量"), QString()});
    const QVariantMap locals = params.value(QStringLiteral("locals")).toMap();
    for (auto it = locals.constBegin(); it != locals.constEnd(); ++it)
        new QTreeWidgetItem(varRoot, {it.key(), variantToDisplay(it.value())});
    varRoot->setExpanded(true);
    m_tabs->setCurrentIndex(0);
}

void ScriptDebugPanel::clearPause()
{
    m_debugTree->clear();
}

void ScriptDebugPanel::onInsertApi(QListWidgetItem *item)
{
    if (!item)
        return;
    QVariantMap p;
    p.insert(QStringLiteral("text"),
             QStringLiteral("app.call(\"%1\", {})\n").arg(item->text()));
    Result<QVariant> out{QVariant()};
    m_app->interactions().dispatchViewAction(kActInsert, p, out);
}

// ============================================================================
// 工厂 + 自注册（视图层 TU 末尾一行，外壳零改动）
// ============================================================================
namespace
{
using PanelFactory = std::function<QWidget *(AppContext &, IIntentExecutor &, QWidget *)>;

QWidget *createScriptFilesPanel(AppContext &app, IIntentExecutor &ex, QWidget *parent)
{
    return new ScriptFilesPanel(app, ex, parent);
}
QWidget *createScriptEditorPanel(AppContext &app, IIntentExecutor &ex, QWidget *parent)
{
    return new ScriptEditorPanel(app, ex, parent);
}
QWidget *createScriptConsolePanel(AppContext &app, IIntentExecutor &ex, QWidget *parent)
{
    return new ScriptConsolePanel(app, ex, parent);
}
QWidget *createScriptDebugPanel(AppContext &app, IIntentExecutor &ex, QWidget *parent)
{
    return new ScriptDebugPanel(app, ex, parent);
}
} // namespace

} // namespace nextpilot::plotscope::script

// 自注册四个停靠面板（标题作停靠/中央页签的显示文本）
NG_REGISTER_PANEL_T("script-files", scriptFiles,
                    nextpilot::plotscope::script::createScriptFilesPanel, "脚本文件")
NG_REGISTER_PANEL_T("script-editor", scriptEditor,
                    nextpilot::plotscope::script::createScriptEditorPanel, "脚本编辑器")
NG_REGISTER_PANEL_T("script-console", scriptConsole,
                    nextpilot::plotscope::script::createScriptConsolePanel, "脚本控制台")
NG_REGISTER_PANEL_T("script-debug", scriptDebug,
                    nextpilot::plotscope::script::createScriptDebugPanel, "脚本调试")
