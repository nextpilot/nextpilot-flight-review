#include "DebugConsolePanel.h"

#include "AppContext.h"
#include "InteractionHub.h"
#include "MessageLog.h"
#include "PanelFactoryHub.h"
#include "Result.h"
#include "ThemeManager.h"

#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::context::Message;
using nextpilot::plotscope::foundation::Result;

namespace
{

QWidget *createDebugConsolePanel(AppContext &appCtx,
                                 nextpilot::plotscope::intent::IIntentExecutor &,
                                 QWidget *parent)
{
    return new DebugConsolePanel(appCtx, parent);
}

} // namespace

DebugConsolePanel::DebugConsolePanel(AppContext &appCtx, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(6);
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    connect(clearBtn, &QPushButton::clicked, this, &DebugConsolePanel::clearConsole);
    bar->addStretch(1);
    bar->addWidget(clearBtn);
    layout->addLayout(bar);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setMaximumBlockCount(context::MessageLog::kMaxMessages);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_view->setFont(mono);
    layout->addWidget(m_view, 1);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(6);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(
        QStringLiteral("调试暂停时可求值局部变量，如 x、#tbl、（表达式）回车执行"));
    auto *runBtn = new QPushButton(QStringLiteral("执行"), this);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(runBtn);
    layout->addLayout(inputRow);

    connect(runBtn, &QPushButton::clicked, this, [this] { evaluate(m_input->text()); });
    connect(m_input, &QLineEdit::returnPressed, this, [this] { evaluate(m_input->text()); });

    connect(&m_appCtx, &AppContext::messageAppended, this,
            [this](const QString &level, const QString &source, const QString &text,
                   const QString &locator) {
                appendMessage(level, source, text, locator);
            });

    reloadFromLog();
    if (m_view->document()->isEmpty())
        m_view->setPlainText(QStringLiteral("-- 调试控制台：断点暂停后在此求值局部变量；"
                                            "脚本跑起来后这里显示暂停/单步/结束事件。"));
}

void DebugConsolePanel::reloadFromLog()
{
    m_view->clear();
    for (const Message &m : m_appCtx.messages().all())
        appendMessage(m.level, m.source, m.text, m.locator);
}

void DebugConsolePanel::appendMessage(const QString &level, const QString &source,
                                      const QString &text, const QString &locator)
{
    Q_UNUSED(source);
    if (level != QStringLiteral("debug"))
        return; // 普通输出归终端/输出，这里只收调试事件
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString color =
        ThemeManager::themeColor(QStringLiteral("textMuted")).name();
    m_view->appendHtml(QStringLiteral("<span style=\"color:%1\">[%2]</span> %3")
                           .arg(color, stamp, text.toHtmlEscaped()));
    if (!locator.isEmpty())
        m_view->appendPlainText(QStringLiteral("    ↳ %1").arg(locator));
}

void DebugConsolePanel::clearConsole()
{
    m_appCtx.messages().clear();
    m_view->clear();
}

void DebugConsolePanel::evaluate(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return;
    m_input->clear();
    m_view->appendPlainText(QStringLiteral("> %1").arg(trimmed));

    // 走与终端同一个意图：调试暂停时由调试引擎求值（能看到断点处局部变量），
    // 未暂停时退化为普通 REPL。两条路径的行为差异由 ScriptModule 决定，
    // 这里不做判断，免得同一语义在面板里再分一份叉。
    QVariantMap p;
    p.insert(QStringLiteral("code"), trimmed);
    Result<QVariant> out{QVariant{}};
    if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("script.workbench.repl"), p, out))
    {
        m_view->appendPlainText(
            QStringLiteral("✘ %1").arg(out.error().message.isEmpty()
                                            ? QStringLiteral("求值失败")
                                            : out.error().message));
        return;
    }
    const QVariant v = out.value();
    m_view->appendPlainText(v.isNull() || !v.isValid()
                                ? QStringLiteral("⇒ nil")
                                : QStringLiteral("⇒ %1").arg(v.toString()));
}

} // namespace nextpilot::plotscope::gui

PS_REGISTER_PANEL_TI("debug-console", debugConsole,
                     ::nextpilot::plotscope::gui::createDebugConsolePanel, "调试控制台",
                     "debug_console")
